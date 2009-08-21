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
#include "radeon_drm.h"
#include "radeon_share.h"

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
int r100_cs_packet_parse_vline(struct radeon_cs_parser *p);
int r100_cs_packet_next_reloc(struct radeon_cs_parser *p,
			      struct radeon_cs_reloc **cs_reloc);
int r100_cs_parse_packet0(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  const unsigned *auth, unsigned n,
			  radeon_packet0_check_t check);
void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt);
int r100_cs_track_check_pkt3_indx_buffer(struct radeon_cs_parser *p,
					 struct radeon_cs_packet *pkt,
					 struct radeon_object *robj);

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
	addr = (lower_32_bits(addr) >> 8) |
	       ((upper_32_bits(addr) & 0xff) << 24) |
	       0xc;
	/* on x86 we want this to be CPU endian, on powerpc
	 * on powerpc without HW swappers, it'll get swapped on way
	 * into VRAM - so no need for cpu_to_le32 on VRAM tables */
	writel(addr, ((void __iomem *)ptr) + (i * 4));
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
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0 ));
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

	r100_vram_init_sizes(rdev);
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

struct r300_cs_track_array {
	struct radeon_object	*robj;
	unsigned		esize;
};

struct r300_cs_track_texture {
	struct radeon_object	*robj;
	unsigned		pitch;
	unsigned		width;
	unsigned		height;
	unsigned		num_levels;
	unsigned		cpp;
	unsigned		tex_coord_type;
	unsigned		txdepth;
	unsigned		width_11;
	unsigned		height_11;
	bool			use_pitch;
	bool			enabled;
	bool			roundup_w;
	bool			roundup_h;
};

struct r300_cs_track {
	unsigned			num_cb;
	unsigned			maxy;
	unsigned			vtx_size;
	unsigned			vap_vf_cntl;
	unsigned			immd_dwords;
	unsigned			num_arrays;
	unsigned			max_indx;
	struct r300_cs_track_array	arrays[11];
	struct r300_cs_track_cb 	cb[4];
	struct r300_cs_track_cb 	zb;
	struct r300_cs_track_texture	textures[16];
	bool				z_enabled;
};

static inline void r300_cs_track_texture_print(struct r300_cs_track_texture *t)
{
	DRM_ERROR("pitch                      %d\n", t->pitch);
	DRM_ERROR("width                      %d\n", t->width);
	DRM_ERROR("height                     %d\n", t->height);
	DRM_ERROR("num levels                 %d\n", t->num_levels);
	DRM_ERROR("depth                      %d\n", t->txdepth);
	DRM_ERROR("bpp                        %d\n", t->cpp);
	DRM_ERROR("coordinate type            %d\n", t->tex_coord_type);
	DRM_ERROR("width round to power of 2  %d\n", t->roundup_w);
	DRM_ERROR("height round to power of 2 %d\n", t->roundup_h);
}

static inline int r300_cs_track_texture_check(struct radeon_device *rdev,
					      struct r300_cs_track *track)
{
	struct radeon_object *robj;
	unsigned long size;
	unsigned u, i, w, h;

	for (u = 0; u < 16; u++) {
		if (!track->textures[u].enabled)
			continue;
		robj = track->textures[u].robj;
		if (robj == NULL) {
			DRM_ERROR("No texture bound to unit %u\n", u);
			return -EINVAL;
		}
		size = 0;
		for (i = 0; i <= track->textures[u].num_levels; i++) {
			if (track->textures[u].use_pitch) {
				w = track->textures[u].pitch / (1 << i);
			} else {
				w = track->textures[u].width / (1 << i);
				if (rdev->family >= CHIP_RV515)
					w |= track->textures[u].width_11;
				if (track->textures[u].roundup_w)
					w = roundup_pow_of_two(w);
			}
			h = track->textures[u].height / (1 << i);
			if (rdev->family >= CHIP_RV515)
				h |= track->textures[u].height_11;
			if (track->textures[u].roundup_h)
				h = roundup_pow_of_two(h);
			size += w * h;
		}
		size *= track->textures[u].cpp;
		switch (track->textures[u].tex_coord_type) {
		case 0:
			break;
		case 1:
			size *= (1 << track->textures[u].txdepth);
			break;
		case 2:
			size *= 6;
			break;
		default:
			DRM_ERROR("Invalid texture coordinate type %u for unit "
				  "%u\n", track->textures[u].tex_coord_type, u);
			return -EINVAL;
		}
		if (size > radeon_object_size(robj)) {
			DRM_ERROR("Texture of unit %u needs %lu bytes but is "
				  "%lu\n", u, size, radeon_object_size(robj));
			r300_cs_track_texture_print(&track->textures[u]);
			return -EINVAL;
		}
	}
	return 0;
}

int r300_cs_track_check(struct radeon_device *rdev, struct r300_cs_track *track)
{
	unsigned i;
	unsigned long size;
	unsigned prim_walk;
	unsigned nverts;

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
	prim_walk = (track->vap_vf_cntl >> 4) & 0x3;
	nverts = (track->vap_vf_cntl >> 16) & 0xFFFF;
	switch (prim_walk) {
	case 1:
		for (i = 0; i < track->num_arrays; i++) {
			size = track->arrays[i].esize * track->max_indx * 4;
			if (track->arrays[i].robj == NULL) {
				DRM_ERROR("(PW %u) Vertex array %u no buffer "
					  "bound\n", prim_walk, i);
				return -EINVAL;
			}
			if (size > radeon_object_size(track->arrays[i].robj)) {
				DRM_ERROR("(PW %u) Vertex array %u need %lu dwords "
					   "have %lu dwords\n", prim_walk, i,
					   size >> 2,
					   radeon_object_size(track->arrays[i].robj) >> 2);
				DRM_ERROR("Max indices %u\n", track->max_indx);
				return -EINVAL;
			}
		}
		break;
	case 2:
		for (i = 0; i < track->num_arrays; i++) {
			size = track->arrays[i].esize * (nverts - 1) * 4;
			if (track->arrays[i].robj == NULL) {
				DRM_ERROR("(PW %u) Vertex array %u no buffer "
					  "bound\n", prim_walk, i);
				return -EINVAL;
			}
			if (size > radeon_object_size(track->arrays[i].robj)) {
				DRM_ERROR("(PW %u) Vertex array %u need %lu dwords "
					   "have %lu dwords\n", prim_walk, i, size >> 2,
					   radeon_object_size(track->arrays[i].robj) >> 2);
				return -EINVAL;
			}
		}
		break;
	case 3:
		size = track->vtx_size * nverts;
		if (size != track->immd_dwords) {
			DRM_ERROR("IMMD draw %u dwors but needs %lu dwords\n",
				  track->immd_dwords, size);
			DRM_ERROR("VAP_VF_CNTL.NUM_VERTICES %u, VTX_SIZE %u\n",
				  nverts, track->vtx_size);
			return -EINVAL;
		}
		break;
	default:
		DRM_ERROR("[drm] Invalid primitive walk %d for VAP_VF_CNTL\n",
			  prim_walk);
		return -EINVAL;
	}
	return r300_cs_track_texture_check(rdev, track);
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
	track->vtx_size = 0x7F;
	track->immd_dwords = 0xFFFFFFFFUL;
	track->num_arrays = 11;
	track->max_indx = 0x00FFFFFFUL;
	for (i = 0; i < track->num_arrays; i++) {
		track->arrays[i].robj = NULL;
		track->arrays[i].esize = 0x7F;
	}
	for (i = 0; i < 16; i++) {
		track->textures[i].pitch = 16536;
		track->textures[i].width = 16536;
		track->textures[i].height = 16536;
		track->textures[i].width_11 = 1 << 11;
		track->textures[i].height_11 = 1 << 11;
		track->textures[i].num_levels = 12;
		track->textures[i].txdepth = 16;
		track->textures[i].cpp = 64;
		track->textures[i].tex_coord_type = 1;
		track->textures[i].robj = NULL;
		/* CS IB emission code makes sure texture unit are disabled */
		track->textures[i].enabled = false;
		track->textures[i].roundup_w = true;
		track->textures[i].roundup_h = true;
	}
}

static const unsigned r300_reg_safe_bm[159] = {
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
	uint32_t tmp, tile_flags = 0;
	unsigned i;
	int r;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	track = (struct r300_cs_track*)p->track;
	switch(reg) {
	case AVIVO_D1MODE_VLINE_START_END:
	case RADEON_CRTC_GUI_TRIG_VLINE:
		r = r100_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		break;
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

		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
			tile_flags |= RADEON_DST_TILE_MACRO;
		if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO) {
			if (reg == RADEON_SRC_PITCH_OFFSET) {
				DRM_ERROR("Cannot src blit from microtiled surface\n");
				r100_cs_dump_packet(p, pkt);
				return -EINVAL;
			}
			tile_flags |= RADEON_DST_TILE_MICRO;
		}
		tmp |= tile_flags;
		ib[idx] = (ib_chunk->kdata[idx] & 0x3fc00000) | tmp;
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
		i = (reg - R300_TX_OFFSET_0) >> 2;
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
		track->textures[i].robj = reloc->robj;
		break;
	/* Tracked registers */
	case 0x2084:
		/* VAP_VF_CNTL */
		track->vap_vf_cntl = ib_chunk->kdata[idx];
		break;
	case 0x20B4:
		/* VAP_VTX_SIZE */
		track->vtx_size = ib_chunk->kdata[idx] & 0x7F;
		break;
	case 0x2134:
		/* VAP_VF_MAX_VTX_INDX */
		track->max_indx = ib_chunk->kdata[idx] & 0x00FFFFFFUL;
		break;
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
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}

		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
			tile_flags |= R300_COLOR_TILE_ENABLE;
		if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
			tile_flags |= R300_COLOR_MICROTILE_ENABLE;

		tmp = ib_chunk->kdata[idx] & ~(0x7 << 16);
		tmp |= tile_flags;
		ib[idx] = tmp;

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
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}

		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
			tile_flags |= R300_DEPTHMACROTILE_ENABLE;
		if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
			tile_flags |= R300_DEPTHMICROTILE_TILED;;

		tmp = ib_chunk->kdata[idx] & ~(0x7 << 16);
		tmp |= tile_flags;
		ib[idx] = tmp;

		track->zb.pitch = ib_chunk->kdata[idx] & 0x3FFC;
		break;
	case 0x4104:
		for (i = 0; i < 16; i++) {
			bool enabled;

			enabled = !!(ib_chunk->kdata[idx] & (1 << i));
			track->textures[i].enabled = enabled;
		}
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
		tmp = (ib_chunk->kdata[idx] >> 25) & 0x3;
		track->textures[i].tex_coord_type = tmp;
		switch ((ib_chunk->kdata[idx] & 0x1F)) {
		case 0:
		case 2:
		case 5:
		case 18:
		case 20:
		case 21:
			track->textures[i].cpp = 1;
			break;
		case 1:
		case 3:
		case 6:
		case 7:
		case 10:
		case 11:
		case 19:
		case 22:
		case 24:
			track->textures[i].cpp = 2;
			break;
		case 4:
		case 8:
		case 9:
		case 12:
		case 13:
		case 23:
		case 25:
		case 27:
		case 30:
			track->textures[i].cpp = 4;
			break;
		case 14:
		case 26:
		case 28:
			track->textures[i].cpp = 8;
			break;
		case 29:
			track->textures[i].cpp = 16;
			break;
		default:
			DRM_ERROR("Invalid texture format %u\n",
				  (ib_chunk->kdata[idx] & 0x1F));
			return -EINVAL;
			break;
		}
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
		tmp = ib_chunk->kdata[idx] & 0x7;;
		if (tmp == 2 || tmp == 4 || tmp == 6) {
			track->textures[i].roundup_w = false;
		}
		tmp = (ib_chunk->kdata[idx] >> 3) & 0x7;;
		if (tmp == 2 || tmp == 4 || tmp == 6) {
			track->textures[i].roundup_h = false;
		}
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
		tmp = ib_chunk->kdata[idx] & 0x3FFF;
		track->textures[i].pitch = tmp + 1;
		if (p->rdev->family >= CHIP_RV515) {
			tmp = ((ib_chunk->kdata[idx] >> 15) & 1) << 11;
			track->textures[i].width_11 = tmp;
			tmp = ((ib_chunk->kdata[idx] >> 16) & 1) << 11;
			track->textures[i].height_11 = tmp;
		}
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
		tmp = ib_chunk->kdata[idx] & 0x7FF;
		track->textures[i].width = tmp + 1;
		tmp = (ib_chunk->kdata[idx] >> 11) & 0x7FF;
		track->textures[i].height = tmp + 1;
		tmp = (ib_chunk->kdata[idx] >> 26) & 0xF;
		track->textures[i].num_levels = tmp;
		tmp = ib_chunk->kdata[idx] & (1 << 31);
		track->textures[i].use_pitch = !!tmp;
		tmp = (ib_chunk->kdata[idx] >> 22) & 0xF;
		track->textures[i].txdepth = tmp;
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n",
		       reg, idx);
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
	track = (struct r300_cs_track*)p->track;
	switch(pkt->opcode) {
	case PACKET3_3D_LOAD_VBPNTR:
		c = ib_chunk->kdata[idx++] & 0x1F;
		track->num_arrays = c;
		for (i = 0; i < (c - 1); i+=2, idx+=3) {
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx+1] = ib_chunk->kdata[idx+1] + ((u32)reloc->lobj.gpu_offset);
			track->arrays[i + 0].robj = reloc->robj;
			track->arrays[i + 0].esize = ib_chunk->kdata[idx] >> 8;
			track->arrays[i + 0].esize &= 0x7F;
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx+2] = ib_chunk->kdata[idx+2] + ((u32)reloc->lobj.gpu_offset);
			track->arrays[i + 1].robj = reloc->robj;
			track->arrays[i + 1].esize = ib_chunk->kdata[idx] >> 24;
			track->arrays[i + 1].esize &= 0x7F;
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
			track->arrays[i + 0].robj = reloc->robj;
			track->arrays[i + 0].esize = ib_chunk->kdata[idx] >> 8;
			track->arrays[i + 0].esize &= 0x7F;
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
		if (((ib_chunk->kdata[idx+1] >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vap_vf_cntl = ib_chunk->kdata[idx+1];
		track->immd_dwords = pkt->count - 1;
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_IMMD_2:
		/* Number of dwords is vtx_size * (num_vertices - 1)
		 * PRIM_WALK must be equal to 3 vertex data in embedded
		 * in cmd stream */
		if (((ib_chunk->kdata[idx] >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vap_vf_cntl = ib_chunk->kdata[idx];
		track->immd_dwords = pkt->count;
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_VBUF:
		track->vap_vf_cntl = ib_chunk->kdata[idx + 1];
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_VBUF_2:
		track->vap_vf_cntl = ib_chunk->kdata[idx];
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_INDX:
		track->vap_vf_cntl = ib_chunk->kdata[idx + 1];
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_INDX_2:
		track->vap_vf_cntl = ib_chunk->kdata[idx];
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
						  p->rdev->config.r300.reg_safe_bm,
						  p->rdev->config.r300.reg_safe_bm_size,
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

int r300_init(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = r300_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(r300_reg_safe_bm);
	return 0;
}
