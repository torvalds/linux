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
#include "radeon_drm.h"
#include "radeon_microcode.h"
#include "radeon_reg.h"
#include "radeon.h"

/* This files gather functions specifics to:
 * r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280
 *
 * Some of these functions might be used by newer ASICs.
 */
void r100_hdp_reset(struct radeon_device *rdev);
void r100_gpu_init(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
int r100_mc_wait_for_idle(struct radeon_device *rdev);
void r100_gpu_wait_for_vsync(struct radeon_device *rdev);
void r100_gpu_wait_for_vsync2(struct radeon_device *rdev);
int r100_debugfs_mc_info_init(struct radeon_device *rdev);


/*
 * PCI GART
 */
void r100_pci_gart_tlb_flush(struct radeon_device *rdev)
{
	/* TODO: can we do somethings here ? */
	/* It seems hw only cache one entry so we should discard this
	 * entry otherwise if first GPU GART read hit this entry it
	 * could end up in wrong address. */
}

int r100_pci_gart_enable(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r) {
		return r;
	}
	if (rdev->gart.table.ram.ptr == NULL) {
		rdev->gart.table_size = rdev->gart.num_gpu_pages * 4;
		r = radeon_gart_table_ram_alloc(rdev);
		if (r) {
			return r;
		}
	}
	/* discard memory request outside of configured range */
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32(RADEON_AIC_CNTL, tmp);
	/* set address range for PCI address translate */
	WREG32(RADEON_AIC_LO_ADDR, rdev->mc.gtt_location);
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	WREG32(RADEON_AIC_HI_ADDR, tmp);
	/* Enable bus mastering */
	tmp = RREG32(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
	WREG32(RADEON_BUS_CNTL, tmp);
	/* set PCI GART page-table base address */
	WREG32(RADEON_AIC_PT_BASE, rdev->gart.table_addr);
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_PCIGART_TRANSLATE_EN;
	WREG32(RADEON_AIC_CNTL, tmp);
	r100_pci_gart_tlb_flush(rdev);
	rdev->gart.ready = true;
	return 0;
}

void r100_pci_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* discard memory request outside of configured range */
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32(RADEON_AIC_CNTL, tmp & ~RADEON_PCIGART_TRANSLATE_EN);
	WREG32(RADEON_AIC_LO_ADDR, 0);
	WREG32(RADEON_AIC_HI_ADDR, 0);
}

int r100_pci_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}
	rdev->gart.table.ram.ptr[i] = cpu_to_le32(lower_32_bits(addr));
	return 0;
}

int r100_gart_enable(struct radeon_device *rdev)
{
	if (rdev->flags & RADEON_IS_AGP) {
		r100_pci_gart_disable(rdev);
		return 0;
	}
	return r100_pci_gart_enable(rdev);
}


/*
 * MC
 */
void r100_mc_disable_clients(struct radeon_device *rdev)
{
	uint32_t ov0_scale_cntl, crtc_ext_cntl, crtc_gen_cntl, crtc2_gen_cntl;

	/* FIXME: is this function correct for rs100,rs200,rs300 ? */
	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	/* stop display and memory access */
	ov0_scale_cntl = RREG32(RADEON_OV0_SCALE_CNTL);
	WREG32(RADEON_OV0_SCALE_CNTL, ov0_scale_cntl & ~RADEON_SCALER_ENABLE);
	crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
	WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl | RADEON_CRTC_DISPLAY_DIS);
	crtc_gen_cntl = RREG32(RADEON_CRTC_GEN_CNTL);

	r100_gpu_wait_for_vsync(rdev);

	WREG32(RADEON_CRTC_GEN_CNTL,
	       (crtc_gen_cntl & ~(RADEON_CRTC_CUR_EN | RADEON_CRTC_ICON_EN)) |
	       RADEON_CRTC_DISP_REQ_EN_B | RADEON_CRTC_EXT_DISP_EN);

	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);

		r100_gpu_wait_for_vsync2(rdev);
		WREG32(RADEON_CRTC2_GEN_CNTL,
		       (crtc2_gen_cntl &
		        ~(RADEON_CRTC2_CUR_EN | RADEON_CRTC2_ICON_EN)) |
		       RADEON_CRTC2_DISP_REQ_EN_B);
	}

	udelay(500);
}

void r100_mc_setup(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	r = r100_debugfs_mc_info_init(rdev);
	if (r) {
		DRM_ERROR("Failed to register debugfs file for R100 MC !\n");
	}
	/* Write VRAM size in case we are limiting it */
	WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
	/* Novell bug 204882 for RN50/M6/M7 with 8/16/32MB VRAM,
	 * if the aperture is 64MB but we have 32MB VRAM
	 * we report only 32MB VRAM but we have to set MC_FB_LOCATION
	 * to 64MB, otherwise the gpu accidentially dies */
	tmp = rdev->mc.vram_location + rdev->mc.mc_vram_size - 1;
	tmp = REG_SET(RADEON_MC_FB_TOP, tmp >> 16);
	tmp |= REG_SET(RADEON_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32(RADEON_MC_FB_LOCATION, tmp);

	/* Enable bus mastering */
	tmp = RREG32(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
	WREG32(RADEON_BUS_CNTL, tmp);

	if (rdev->flags & RADEON_IS_AGP) {
		tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
		tmp = REG_SET(RADEON_MC_AGP_TOP, tmp >> 16);
		tmp |= REG_SET(RADEON_MC_AGP_START, rdev->mc.gtt_location >> 16);
		WREG32(RADEON_MC_AGP_LOCATION, tmp);
		WREG32(RADEON_AGP_BASE, rdev->mc.agp_base);
	} else {
		WREG32(RADEON_MC_AGP_LOCATION, 0x0FFFFFFF);
		WREG32(RADEON_AGP_BASE, 0);
	}

	tmp = RREG32(RADEON_HOST_PATH_CNTL) & RADEON_HDP_APER_CNTL;
	tmp |= (7 << 28);
	WREG32(RADEON_HOST_PATH_CNTL, tmp | RADEON_HDP_SOFT_RESET | RADEON_HDP_READ_BUFFER_INVALIDATE);
	(void)RREG32(RADEON_HOST_PATH_CNTL);
	WREG32(RADEON_HOST_PATH_CNTL, tmp);
	(void)RREG32(RADEON_HOST_PATH_CNTL);
}

int r100_mc_init(struct radeon_device *rdev)
{
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}

	r100_gpu_init(rdev);
	/* Disable gart which also disable out of gart access */
	r100_pci_gart_disable(rdev);

	/* Setup GPU memory space */
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

	r100_mc_disable_clients(rdev);
	if (r100_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	r100_mc_setup(rdev);
	return 0;
}

void r100_mc_fini(struct radeon_device *rdev)
{
	r100_pci_gart_disable(rdev);
	radeon_gart_table_ram_free(rdev);
	radeon_gart_fini(rdev);
}


/*
 * Fence emission
 */
void r100_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence)
{
	/* Who ever call radeon_fence_emit should call ring_lock and ask
	 * for enough space (today caller are ib schedule and buffer move) */
	/* Wait until IDLE & CLEAN */
	radeon_ring_write(rdev, PACKET0(0x1720, 0));
	radeon_ring_write(rdev, (1 << 16) | (1 << 17));
	/* Emit fence sequence & fire IRQ */
	radeon_ring_write(rdev, PACKET0(rdev->fence_drv.scratch_reg, 0));
	radeon_ring_write(rdev, fence->seq);
	radeon_ring_write(rdev, PACKET0(RADEON_GEN_INT_STATUS, 0));
	radeon_ring_write(rdev, RADEON_SW_INT_FIRE);
}


/*
 * Writeback
 */
int r100_wb_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->wb.wb_obj == NULL) {
		r = radeon_object_create(rdev, NULL, 4096,
					 true,
					 RADEON_GEM_DOMAIN_GTT,
					 false, &rdev->wb.wb_obj);
		if (r) {
			DRM_ERROR("radeon: failed to create WB buffer (%d).\n", r);
			return r;
		}
		r = radeon_object_pin(rdev->wb.wb_obj,
				      RADEON_GEM_DOMAIN_GTT,
				      &rdev->wb.gpu_addr);
		if (r) {
			DRM_ERROR("radeon: failed to pin WB buffer (%d).\n", r);
			return r;
		}
		r = radeon_object_kmap(rdev->wb.wb_obj, (void **)&rdev->wb.wb);
		if (r) {
			DRM_ERROR("radeon: failed to map WB buffer (%d).\n", r);
			return r;
		}
	}
	WREG32(0x774, rdev->wb.gpu_addr);
	WREG32(0x70C, rdev->wb.gpu_addr + 1024);
	WREG32(0x770, 0xff);
	return 0;
}

void r100_wb_fini(struct radeon_device *rdev)
{
	if (rdev->wb.wb_obj) {
		radeon_object_kunmap(rdev->wb.wb_obj);
		radeon_object_unpin(rdev->wb.wb_obj);
		radeon_object_unref(&rdev->wb.wb_obj);
		rdev->wb.wb = NULL;
		rdev->wb.wb_obj = NULL;
	}
}

int r100_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset,
		   uint64_t dst_offset,
		   unsigned num_pages,
		   struct radeon_fence *fence)
{
	uint32_t cur_pages;
	uint32_t stride_bytes = PAGE_SIZE;
	uint32_t pitch;
	uint32_t stride_pixels;
	unsigned ndw;
	int num_loops;
	int r = 0;

	/* radeon limited to 16k stride */
	stride_bytes &= 0x3fff;
	/* radeon pitch is /64 */
	pitch = stride_bytes / 64;
	stride_pixels = stride_bytes / 4;
	num_loops = DIV_ROUND_UP(num_pages, 8191);

	/* Ask for enough room for blit + flush + fence */
	ndw = 64 + (10 * num_loops);
	r = radeon_ring_lock(rdev, ndw);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d) asking for %u dw.\n", r, ndw);
		return -EINVAL;
	}
	while (num_pages > 0) {
		cur_pages = num_pages;
		if (cur_pages > 8191) {
			cur_pages = 8191;
		}
		num_pages -= cur_pages;

		/* pages are in Y direction - height
		   page width in X direction - width */
		radeon_ring_write(rdev, PACKET3(PACKET3_BITBLT_MULTI, 8));
		radeon_ring_write(rdev,
				  RADEON_GMC_SRC_PITCH_OFFSET_CNTL |
				  RADEON_GMC_DST_PITCH_OFFSET_CNTL |
				  RADEON_GMC_SRC_CLIPPING |
				  RADEON_GMC_DST_CLIPPING |
				  RADEON_GMC_BRUSH_NONE |
				  (RADEON_COLOR_FORMAT_ARGB8888 << 8) |
				  RADEON_GMC_SRC_DATATYPE_COLOR |
				  RADEON_ROP3_S |
				  RADEON_DP_SRC_SOURCE_MEMORY |
				  RADEON_GMC_CLR_CMP_CNTL_DIS |
				  RADEON_GMC_WR_MSK_DIS);
		radeon_ring_write(rdev, (pitch << 22) | (src_offset >> 10));
		radeon_ring_write(rdev, (pitch << 22) | (dst_offset >> 10));
		radeon_ring_write(rdev, (0x1fff) | (0x1fff << 16));
		radeon_ring_write(rdev, 0);
		radeon_ring_write(rdev, (0x1fff) | (0x1fff << 16));
		radeon_ring_write(rdev, num_pages);
		radeon_ring_write(rdev, num_pages);
		radeon_ring_write(rdev, cur_pages | (stride_pixels << 16));
	}
	radeon_ring_write(rdev, PACKET0(RADEON_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, RADEON_RB2D_DC_FLUSH_ALL);
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_HOST_IDLECLEAN |
			  RADEON_WAIT_DMA_GUI_IDLE);
	if (fence) {
		r = radeon_fence_emit(rdev, fence);
	}
	radeon_ring_unlock_commit(rdev);
	return r;
}


/*
 * CP
 */
void r100_ring_start(struct radeon_device *rdev)
{
	int r;

	r = radeon_ring_lock(rdev, 2);
	if (r) {
		return;
	}
	radeon_ring_write(rdev, PACKET0(RADEON_ISYNC_CNTL, 0));
	radeon_ring_write(rdev,
			  RADEON_ISYNC_ANY2D_IDLE3D |
			  RADEON_ISYNC_ANY3D_IDLE2D |
			  RADEON_ISYNC_WAIT_IDLEGUI |
			  RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_unlock_commit(rdev);
}

static void r100_cp_load_microcode(struct radeon_device *rdev)
{
	int i;

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	WREG32(RADEON_CP_ME_RAM_ADDR, 0);
	if ((rdev->family == CHIP_R100) || (rdev->family == CHIP_RV100) ||
	    (rdev->family == CHIP_RV200) || (rdev->family == CHIP_RS100) ||
	    (rdev->family == CHIP_RS200)) {
		DRM_INFO("Loading R100 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, R100_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, R100_cp_microcode[i][0]);
		}
	} else if ((rdev->family == CHIP_R200) ||
		   (rdev->family == CHIP_RV250) ||
		   (rdev->family == CHIP_RV280) ||
		   (rdev->family == CHIP_RS300)) {
		DRM_INFO("Loading R200 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, R200_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, R200_cp_microcode[i][0]);
		}
	} else if ((rdev->family == CHIP_R300) ||
		   (rdev->family == CHIP_R350) ||
		   (rdev->family == CHIP_RV350) ||
		   (rdev->family == CHIP_RV380) ||
		   (rdev->family == CHIP_RS400) ||
		   (rdev->family == CHIP_RS480)) {
		DRM_INFO("Loading R300 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, R300_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, R300_cp_microcode[i][0]);
		}
	} else if ((rdev->family == CHIP_R420) ||
		   (rdev->family == CHIP_R423) ||
		   (rdev->family == CHIP_RV410)) {
		DRM_INFO("Loading R400 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, R420_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, R420_cp_microcode[i][0]);
		}
	} else if ((rdev->family == CHIP_RS690) ||
		   (rdev->family == CHIP_RS740)) {
		DRM_INFO("Loading RS690/RS740 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, RS690_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, RS690_cp_microcode[i][0]);
		}
	} else if (rdev->family == CHIP_RS600) {
		DRM_INFO("Loading RS600 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, RS600_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, RS600_cp_microcode[i][0]);
		}
	} else if ((rdev->family == CHIP_RV515) ||
		   (rdev->family == CHIP_R520) ||
		   (rdev->family == CHIP_RV530) ||
		   (rdev->family == CHIP_R580) ||
		   (rdev->family == CHIP_RV560) ||
		   (rdev->family == CHIP_RV570)) {
		DRM_INFO("Loading R500 Microcode\n");
		for (i = 0; i < 256; i++) {
			WREG32(RADEON_CP_ME_RAM_DATAH, R520_cp_microcode[i][1]);
			WREG32(RADEON_CP_ME_RAM_DATAL, R520_cp_microcode[i][0]);
		}
	}
}

int r100_cp_init(struct radeon_device *rdev, unsigned ring_size)
{
	unsigned rb_bufsz;
	unsigned rb_blksz;
	unsigned max_fetch;
	unsigned pre_write_timer;
	unsigned pre_write_limit;
	unsigned indirect2_start;
	unsigned indirect1_start;
	uint32_t tmp;
	int r;

	if (r100_debugfs_cp_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for CP !\n");
	}
	/* Reset CP */
	tmp = RREG32(RADEON_CP_CSQ_STAT);
	if ((tmp & (1 << 31))) {
		DRM_INFO("radeon: cp busy (0x%08X) resetting\n", tmp);
		WREG32(RADEON_CP_CSQ_MODE, 0);
		WREG32(RADEON_CP_CSQ_CNTL, 0);
		WREG32(RADEON_RBBM_SOFT_RESET, RADEON_SOFT_RESET_CP);
		tmp = RREG32(RADEON_RBBM_SOFT_RESET);
		mdelay(2);
		WREG32(RADEON_RBBM_SOFT_RESET, 0);
		tmp = RREG32(RADEON_RBBM_SOFT_RESET);
		mdelay(2);
		tmp = RREG32(RADEON_CP_CSQ_STAT);
		if ((tmp & (1 << 31))) {
			DRM_INFO("radeon: cp reset failed (0x%08X)\n", tmp);
		}
	} else {
		DRM_INFO("radeon: cp idle (0x%08X)\n", tmp);
	}
	/* Align ring size */
	rb_bufsz = drm_order(ring_size / 8);
	ring_size = (1 << (rb_bufsz + 1)) * 4;
	r100_cp_load_microcode(rdev);
	r = radeon_ring_init(rdev, ring_size);
	if (r) {
		return r;
	}
	/* Each time the cp read 1024 bytes (16 dword/quadword) update
	 * the rptr copy in system ram */
	rb_blksz = 9;
	/* cp will read 128bytes at a time (4 dwords) */
	max_fetch = 1;
	rdev->cp.align_mask = 16 - 1;
	/* Write to CP_RB_WPTR will be delayed for pre_write_timer clocks */
	pre_write_timer = 64;
	/* Force CP_RB_WPTR write if written more than one time before the
	 * delay expire
	 */
	pre_write_limit = 0;
	/* Setup the cp cache like this (cache size is 96 dwords) :
	 *	RING		0  to 15
	 *	INDIRECT1	16 to 79
	 *	INDIRECT2	80 to 95
	 * So ring cache size is 16dwords (> (2 * max_fetch = 2 * 4dwords))
	 *    indirect1 cache size is 64dwords (> (2 * max_fetch = 2 * 4dwords))
	 *    indirect2 cache size is 16dwords (> (2 * max_fetch = 2 * 4dwords))
	 * Idea being that most of the gpu cmd will be through indirect1 buffer
	 * so it gets the bigger cache.
	 */
	indirect2_start = 80;
	indirect1_start = 16;
	/* cp setup */
	WREG32(0x718, pre_write_timer | (pre_write_limit << 28));
	WREG32(RADEON_CP_RB_CNTL,
#ifdef __BIG_ENDIAN
	       RADEON_BUF_SWAP_32BIT |
#endif
	       REG_SET(RADEON_RB_BUFSZ, rb_bufsz) |
	       REG_SET(RADEON_RB_BLKSZ, rb_blksz) |
	       REG_SET(RADEON_MAX_FETCH, max_fetch) |
	       RADEON_RB_NO_UPDATE);
	/* Set ring address */
	DRM_INFO("radeon: ring at 0x%016lX\n", (unsigned long)rdev->cp.gpu_addr);
	WREG32(RADEON_CP_RB_BASE, rdev->cp.gpu_addr);
	/* Force read & write ptr to 0 */
	tmp = RREG32(RADEON_CP_RB_CNTL);
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA);
	WREG32(RADEON_CP_RB_RPTR_WR, 0);
	WREG32(RADEON_CP_RB_WPTR, 0);
	WREG32(RADEON_CP_RB_CNTL, tmp);
	udelay(10);
	rdev->cp.rptr = RREG32(RADEON_CP_RB_RPTR);
	rdev->cp.wptr = RREG32(RADEON_CP_RB_WPTR);
	/* Set cp mode to bus mastering & enable cp*/
	WREG32(RADEON_CP_CSQ_MODE,
	       REG_SET(RADEON_INDIRECT2_START, indirect2_start) |
	       REG_SET(RADEON_INDIRECT1_START, indirect1_start));
	WREG32(0x718, 0);
	WREG32(0x744, 0x00004D4D);
	WREG32(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIBM_INDBM);
	radeon_ring_start(rdev);
	r = radeon_ring_test(rdev);
	if (r) {
		DRM_ERROR("radeon: cp isn't working (%d).\n", r);
		return r;
	}
	rdev->cp.ready = true;
	return 0;
}

void r100_cp_fini(struct radeon_device *rdev)
{
	/* Disable ring */
	rdev->cp.ready = false;
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	radeon_ring_fini(rdev);
	DRM_INFO("radeon: cp finalized\n");
}

void r100_cp_disable(struct radeon_device *rdev)
{
	/* Disable ring */
	rdev->cp.ready = false;
	WREG32(RADEON_CP_CSQ_MODE, 0);
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}

int r100_cp_reset(struct radeon_device *rdev)
{
	uint32_t tmp;
	bool reinit_cp;
	int i;

	reinit_cp = rdev->cp.ready;
	rdev->cp.ready = false;
	WREG32(RADEON_CP_CSQ_MODE, 0);
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	WREG32(RADEON_RBBM_SOFT_RESET, RADEON_SOFT_RESET_CP);
	(void)RREG32(RADEON_RBBM_SOFT_RESET);
	udelay(200);
	WREG32(RADEON_RBBM_SOFT_RESET, 0);
	/* Wait to prevent race in RBBM_STATUS */
	mdelay(1);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & (1 << 16))) {
			DRM_INFO("CP reset succeed (RBBM_STATUS=0x%08X)\n",
				 tmp);
			if (reinit_cp) {
				return r100_cp_init(rdev, rdev->cp.ring_size);
			}
			return 0;
		}
		DRM_UDELAY(1);
	}
	tmp = RREG32(RADEON_RBBM_STATUS);
	DRM_ERROR("Failed to reset CP (RBBM_STATUS=0x%08X)!\n", tmp);
	return -1;
}


/*
 * CS functions
 */
int r100_cs_parse_packet0(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  const unsigned *auth, unsigned n,
			  radeon_packet0_check_t check)
{
	unsigned reg;
	unsigned i, j, m;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	/* Check that register fall into register range
	 * determined by the number of entry (n) in the
	 * safe register bitmap.
	 */
	if (pkt->one_reg_wr) {
		if ((reg >> 7) > n) {
			return -EINVAL;
		}
	} else {
		if (((reg + (pkt->count << 2)) >> 7) > n) {
			return -EINVAL;
		}
	}
	for (i = 0; i <= pkt->count; i++, idx++) {
		j = (reg >> 7);
		m = 1 << ((reg >> 2) & 31);
		if (auth[j] & m) {
			r = check(p, pkt, idx, reg);
			if (r) {
				return r;
			}
		}
		if (pkt->one_reg_wr) {
			if (!(auth[j] & m)) {
				break;
			}
		} else {
			reg += 4;
		}
	}
	return 0;
}

void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt)
{
	struct radeon_cs_chunk *ib_chunk;
	volatile uint32_t *ib;
	unsigned i;
	unsigned idx;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	idx = pkt->idx;
	for (i = 0; i <= (pkt->count + 1); i++, idx++) {
		DRM_INFO("ib[%d]=0x%08X\n", idx, ib[idx]);
	}
}

/**
 * r100_cs_packet_parse() - parse cp packet and point ib index to next packet
 * @parser:	parser structure holding parsing context.
 * @pkt:	where to store packet informations
 *
 * Assume that chunk_ib_index is properly set. Will return -EINVAL
 * if packet is bigger than remaining ib size. or if packets is unknown.
 **/
int r100_cs_packet_parse(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt,
			 unsigned idx)
{
	struct radeon_cs_chunk *ib_chunk = &p->chunks[p->chunk_ib_idx];
	uint32_t header = ib_chunk->kdata[idx];

	if (idx >= ib_chunk->length_dw) {
		DRM_ERROR("Can not parse packet at %d after CS end %d !\n",
			  idx, ib_chunk->length_dw);
		return -EINVAL;
	}
	pkt->idx = idx;
	pkt->type = CP_PACKET_GET_TYPE(header);
	pkt->count = CP_PACKET_GET_COUNT(header);
	switch (pkt->type) {
	case PACKET_TYPE0:
		pkt->reg = CP_PACKET0_GET_REG(header);
		pkt->one_reg_wr = CP_PACKET0_GET_ONE_REG_WR(header);
		break;
	case PACKET_TYPE3:
		pkt->opcode = CP_PACKET3_GET_OPCODE(header);
		break;
	case PACKET_TYPE2:
		pkt->count = -1;
		break;
	default:
		DRM_ERROR("Unknown packet type %d at %d !\n", pkt->type, idx);
		return -EINVAL;
	}
	if ((pkt->count + 1 + pkt->idx) >= ib_chunk->length_dw) {
		DRM_ERROR("Packet (%d:%d:%d) end after CS buffer (%d) !\n",
			  pkt->idx, pkt->type, pkt->count, ib_chunk->length_dw);
		return -EINVAL;
	}
	return 0;
}

/**
 * r100_cs_packet_next_vline() - parse userspace VLINE packet
 * @parser:		parser structure holding parsing context.
 *
 * Userspace sends a special sequence for VLINE waits.
 * PACKET0 - VLINE_START_END + value
 * PACKET0 - WAIT_UNTIL +_value
 * RELOC (P3) - crtc_id in reloc.
 *
 * This function parses this and relocates the VLINE START END
 * and WAIT UNTIL packets to the correct crtc.
 * It also detects a switched off crtc and nulls out the
 * wait in that case.
 */
int r100_cs_packet_parse_vline(struct radeon_cs_parser *p)
{
	struct radeon_cs_chunk *ib_chunk;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	struct radeon_cs_packet p3reloc, waitreloc;
	int crtc_id;
	int r;
	uint32_t header, h_idx, reg;

	ib_chunk = &p->chunks[p->chunk_ib_idx];

	/* parse the wait until */
	r = r100_cs_packet_parse(p, &waitreloc, p->idx);
	if (r)
		return r;

	/* check its a wait until and only 1 count */
	if (waitreloc.reg != RADEON_WAIT_UNTIL ||
	    waitreloc.count != 0) {
		DRM_ERROR("vline wait had illegal wait until segment\n");
		r = -EINVAL;
		return r;
	}

	if (ib_chunk->kdata[waitreloc.idx + 1] != RADEON_WAIT_CRTC_VLINE) {
		DRM_ERROR("vline wait had illegal wait until\n");
		r = -EINVAL;
		return r;
	}

	/* jump over the NOP */
	r = r100_cs_packet_parse(p, &p3reloc, p->idx);
	if (r)
		return r;

	h_idx = p->idx - 2;
	p->idx += waitreloc.count;
	p->idx += p3reloc.count;

	header = ib_chunk->kdata[h_idx];
	crtc_id = ib_chunk->kdata[h_idx + 5];
	reg = ib_chunk->kdata[h_idx] >> 2;
	mutex_lock(&p->rdev->ddev->mode_config.mutex);
	obj = drm_mode_object_find(p->rdev->ddev, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_ERROR("cannot find crtc %d\n", crtc_id);
		r = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);
	radeon_crtc = to_radeon_crtc(crtc);
	crtc_id = radeon_crtc->crtc_id;

	if (!crtc->enabled) {
		/* if the CRTC isn't enabled - we need to nop out the wait until */
		ib_chunk->kdata[h_idx + 2] = PACKET2(0);
		ib_chunk->kdata[h_idx + 3] = PACKET2(0);
	} else if (crtc_id == 1) {
		switch (reg) {
		case AVIVO_D1MODE_VLINE_START_END:
			header &= R300_CP_PACKET0_REG_MASK;
			header |= AVIVO_D2MODE_VLINE_START_END >> 2;
			break;
		case RADEON_CRTC_GUI_TRIG_VLINE:
			header &= R300_CP_PACKET0_REG_MASK;
			header |= RADEON_CRTC2_GUI_TRIG_VLINE >> 2;
			break;
		default:
			DRM_ERROR("unknown crtc reloc\n");
			r = -EINVAL;
			goto out;
		}
		ib_chunk->kdata[h_idx] = header;
		ib_chunk->kdata[h_idx + 3] |= RADEON_ENG_DISPLAY_SELECT_CRTC1;
	}
out:
	mutex_unlock(&p->rdev->ddev->mode_config.mutex);
	return r;
}

/**
 * r100_cs_packet_next_reloc() - parse next packet which should be reloc packet3
 * @parser:		parser structure holding parsing context.
 * @data:		pointer to relocation data
 * @offset_start:	starting offset
 * @offset_mask:	offset mask (to align start offset on)
 * @reloc:		reloc informations
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
int r100_cs_packet_next_reloc(struct radeon_cs_parser *p,
			      struct radeon_cs_reloc **cs_reloc)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_packet p3reloc;
	unsigned idx;
	int r;

	if (p->chunk_relocs_idx == -1) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	*cs_reloc = NULL;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	relocs_chunk = &p->chunks[p->chunk_relocs_idx];
	r = r100_cs_packet_parse(p, &p3reloc, p->idx);
	if (r) {
		return r;
	}
	p->idx += p3reloc.count + 2;
	if (p3reloc.type != PACKET_TYPE3 || p3reloc.opcode != PACKET3_NOP) {
		DRM_ERROR("No packet3 for relocation for packet at %d.\n",
			  p3reloc.idx);
		r100_cs_dump_packet(p, &p3reloc);
		return -EINVAL;
	}
	idx = ib_chunk->kdata[p3reloc.idx + 1];
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		r100_cs_dump_packet(p, &p3reloc);
		return -EINVAL;
	}
	/* FIXME: we assume reloc size is 4 dwords */
	*cs_reloc = p->relocs_ptr[(idx / 4)];
	return 0;
}

static int r100_packet0_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_cs_reloc *reloc;
	volatile uint32_t *ib;
	uint32_t tmp;
	unsigned reg;
	unsigned i;
	unsigned idx;
	bool onereg;
	int r;
	u32 tile_flags = 0;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	idx = pkt->idx + 1;
	reg = pkt->reg;
	onereg = false;
	if (CP_PACKET0_GET_ONE_REG_WR(ib_chunk->kdata[pkt->idx])) {
		onereg = true;
	}
	for (i = 0; i <= pkt->count; i++, idx++, reg += 4) {
		switch (reg) {
		case RADEON_CRTC_GUI_TRIG_VLINE:
			r = r100_cs_packet_parse_vline(p);
			if (r) {
				DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
						idx, reg);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			break;
		/* FIXME: only allow PACKET3 blit? easier to check for out of
		 * range access */
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
		case RADEON_RB3D_DEPTHOFFSET:
		case RADEON_RB3D_COLOROFFSET:
		case R300_RB3D_COLOROFFSET0:
		case R300_ZB_DEPTHOFFSET:
		case R200_PP_TXOFFSET_0:
		case R200_PP_TXOFFSET_1:
		case R200_PP_TXOFFSET_2:
		case R200_PP_TXOFFSET_3:
		case R200_PP_TXOFFSET_4:
		case R200_PP_TXOFFSET_5:
		case RADEON_PP_TXOFFSET_0:
		case RADEON_PP_TXOFFSET_1:
		case RADEON_PP_TXOFFSET_2:
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
			/* rn50 has no 3D engine so fail on any 3d setup */
			if (ASIC_IS_RN50(p->rdev)) {
				DRM_ERROR("attempt to use RN50 3D engine failed\n");
				return -EINVAL;
			}
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					  idx, reg);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
			break;
		case R300_RB3D_COLORPITCH0:
		case RADEON_RB3D_COLORPITCH:
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					  idx, reg);
				r100_cs_dump_packet(p, pkt);
				return r;
			}

			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= RADEON_COLOR_TILE_ENABLE;
			if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= RADEON_COLOR_MICROTILE_ENABLE;

			tmp = ib_chunk->kdata[idx] & ~(0x7 << 16);
			tmp |= tile_flags;
			ib[idx] = tmp;
			break;
		default:
			/* FIXME: we don't want to allow anyothers packet */
			break;
		}
		if (onereg) {
			/* FIXME: forbid onereg write to register on relocate */
			break;
		}
	}
	return 0;
}

int r100_cs_track_check_pkt3_indx_buffer(struct radeon_cs_parser *p,
					 struct radeon_cs_packet *pkt,
					 struct radeon_object *robj)
{
	struct radeon_cs_chunk *ib_chunk;
	unsigned idx;

	ib_chunk = &p->chunks[p->chunk_ib_idx];
	idx = pkt->idx + 1;
	if ((ib_chunk->kdata[idx+2] + 1) > radeon_object_size(robj)) {
		DRM_ERROR("[drm] Buffer too small for PACKET3 INDX_BUFFER "
			  "(need %u have %lu) !\n",
			  ib_chunk->kdata[idx+2] + 1,
			  radeon_object_size(robj));
		return -EINVAL;
	}
	return 0;
}

static int r100_packet3_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_cs_reloc *reloc;
	unsigned idx;
	unsigned i, c;
	volatile uint32_t *ib;
	int r;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	idx = pkt->idx + 1;
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
		r = r100_cs_track_check_pkt3_indx_buffer(p, pkt, reloc->robj);
		if (r) {
			return r;
		}
		break;
	case 0x23:
		/* FIXME: cleanup */
		/* 3D_RNDR_GEN_INDX_PRIM on r100/r200 */
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n", pkt->opcode);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
		break;
	case PACKET3_3D_DRAW_IMMD:
		/* triggers drawing using in-packet vertex data */
	case PACKET3_3D_DRAW_IMMD_2:
		/* triggers drawing using in-packet vertex data */
	case PACKET3_3D_DRAW_VBUF_2:
		/* triggers drawing of vertex buffers setup elsewhere */
	case PACKET3_3D_DRAW_INDX_2:
		/* triggers drawing using indices to vertex buffer */
	case PACKET3_3D_DRAW_VBUF:
		/* triggers drawing of vertex buffers setup elsewhere */
	case PACKET3_3D_DRAW_INDX:
		/* triggers drawing using indices to vertex buffer */
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int r100_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	int r;

	do {
		r = r100_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
			case PACKET_TYPE0:
				r = r100_packet0_check(p, &pkt);
				break;
			case PACKET_TYPE2:
				break;
			case PACKET_TYPE3:
				r = r100_packet3_check(p, &pkt);
				break;
			default:
				DRM_ERROR("Unknown packet type %d !\n",
					  pkt.type);
				return -EINVAL;
		}
		if (r) {
			return r;
		}
	} while (p->idx < p->chunks[p->chunk_ib_idx].length_dw);
	return 0;
}


/*
 * Global GPU functions
 */
void r100_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;

	if (rdev->family == CHIP_RV200 || rdev->family == CHIP_RS200) {
		rdev->pll_errata |= CHIP_ERRATA_PLL_DUMMYREADS;
	}

	if (rdev->family == CHIP_RV100 ||
	    rdev->family == CHIP_RS100 ||
	    rdev->family == CHIP_RS200) {
		rdev->pll_errata |= CHIP_ERRATA_PLL_DELAY;
	}
}

/* Wait for vertical sync on primary CRTC */
void r100_gpu_wait_for_vsync(struct radeon_device *rdev)
{
	uint32_t crtc_gen_cntl, tmp;
	int i;

	crtc_gen_cntl = RREG32(RADEON_CRTC_GEN_CNTL);
	if ((crtc_gen_cntl & RADEON_CRTC_DISP_REQ_EN_B) ||
	    !(crtc_gen_cntl & RADEON_CRTC_EN)) {
		return;
	}
	/* Clear the CRTC_VBLANK_SAVE bit */
	WREG32(RADEON_CRTC_STATUS, RADEON_CRTC_VBLANK_SAVE_CLEAR);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_CRTC_STATUS);
		if (tmp & RADEON_CRTC_VBLANK_SAVE) {
			return;
		}
		DRM_UDELAY(1);
	}
}

/* Wait for vertical sync on secondary CRTC */
void r100_gpu_wait_for_vsync2(struct radeon_device *rdev)
{
	uint32_t crtc2_gen_cntl, tmp;
	int i;

	crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);
	if ((crtc2_gen_cntl & RADEON_CRTC2_DISP_REQ_EN_B) ||
	    !(crtc2_gen_cntl & RADEON_CRTC2_EN))
		return;

	/* Clear the CRTC_VBLANK_SAVE bit */
	WREG32(RADEON_CRTC2_STATUS, RADEON_CRTC2_VBLANK_SAVE_CLEAR);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_CRTC2_STATUS);
		if (tmp & RADEON_CRTC2_VBLANK_SAVE) {
			return;
		}
		DRM_UDELAY(1);
	}
}

int r100_rbbm_fifo_wait_for_entry(struct radeon_device *rdev, unsigned n)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK;
		if (tmp >= n) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

int r100_gui_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	if (r100_rbbm_fifo_wait_for_entry(rdev, 64)) {
		printk(KERN_WARNING "radeon: wait for empty RBBM fifo failed !"
		       " Bad things might happen.\n");
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & (1 << 31))) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

int r100_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(0x0150);
		if (tmp & (1 << 2)) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

void r100_gpu_init(struct radeon_device *rdev)
{
	/* TODO: anythings to do here ? pipes ? */
	r100_hdp_reset(rdev);
}

void r100_hdp_reset(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32(RADEON_HOST_PATH_CNTL) & RADEON_HDP_APER_CNTL;
	tmp |= (7 << 28);
	WREG32(RADEON_HOST_PATH_CNTL, tmp | RADEON_HDP_SOFT_RESET | RADEON_HDP_READ_BUFFER_INVALIDATE);
	(void)RREG32(RADEON_HOST_PATH_CNTL);
	udelay(200);
	WREG32(RADEON_RBBM_SOFT_RESET, 0);
	WREG32(RADEON_HOST_PATH_CNTL, tmp);
	(void)RREG32(RADEON_HOST_PATH_CNTL);
}

int r100_rb2d_reset(struct radeon_device *rdev)
{
	uint32_t tmp;
	int i;

	WREG32(RADEON_RBBM_SOFT_RESET, RADEON_SOFT_RESET_E2);
	(void)RREG32(RADEON_RBBM_SOFT_RESET);
	udelay(200);
	WREG32(RADEON_RBBM_SOFT_RESET, 0);
	/* Wait to prevent race in RBBM_STATUS */
	mdelay(1);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & (1 << 26))) {
			DRM_INFO("RB2D reset succeed (RBBM_STATUS=0x%08X)\n",
				 tmp);
			return 0;
		}
		DRM_UDELAY(1);
	}
	tmp = RREG32(RADEON_RBBM_STATUS);
	DRM_ERROR("Failed to reset RB2D (RBBM_STATUS=0x%08X)!\n", tmp);
	return -1;
}

int r100_gpu_reset(struct radeon_device *rdev)
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
	/* TODO: reset 3D engine */
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
 * VRAM info
 */
static void r100_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;

	rdev->mc.vram_is_ddr = false;
	if (rdev->flags & RADEON_IS_IGP)
		rdev->mc.vram_is_ddr = true;
	else if (RREG32(RADEON_MEM_SDRAM_MODE_REG) & RADEON_MEM_CFG_TYPE_DDR)
		rdev->mc.vram_is_ddr = true;
	if ((rdev->family == CHIP_RV100) ||
	    (rdev->family == CHIP_RS100) ||
	    (rdev->family == CHIP_RS200)) {
		tmp = RREG32(RADEON_MEM_CNTL);
		if (tmp & RV100_HALF_MODE) {
			rdev->mc.vram_width = 32;
		} else {
			rdev->mc.vram_width = 64;
		}
		if (rdev->flags & RADEON_SINGLE_CRTC) {
			rdev->mc.vram_width /= 4;
			rdev->mc.vram_is_ddr = true;
		}
	} else if (rdev->family <= CHIP_RV280) {
		tmp = RREG32(RADEON_MEM_CNTL);
		if (tmp & RADEON_MEM_NUM_CHANNELS_MASK) {
			rdev->mc.vram_width = 128;
		} else {
			rdev->mc.vram_width = 64;
		}
	} else {
		/* newer IGPs */
		rdev->mc.vram_width = 128;
	}
}

static u32 r100_get_accessible_vram(struct radeon_device *rdev)
{
	u32 aper_size;
	u8 byte;

	aper_size = RREG32(RADEON_CONFIG_APER_SIZE);

	/* Set HDP_APER_CNTL only on cards that are known not to be broken,
	 * that is has the 2nd generation multifunction PCI interface
	 */
	if (rdev->family == CHIP_RV280 ||
	    rdev->family >= CHIP_RV350) {
		WREG32_P(RADEON_HOST_PATH_CNTL, RADEON_HDP_APER_CNTL,
		       ~RADEON_HDP_APER_CNTL);
		DRM_INFO("Generation 2 PCI interface, using max accessible memory\n");
		return aper_size * 2;
	}

	/* Older cards have all sorts of funny issues to deal with. First
	 * check if it's a multifunction card by reading the PCI config
	 * header type... Limit those to one aperture size
	 */
	pci_read_config_byte(rdev->pdev, 0xe, &byte);
	if (byte & 0x80) {
		DRM_INFO("Generation 1 PCI interface in multifunction mode\n");
		DRM_INFO("Limiting VRAM to one aperture\n");
		return aper_size;
	}

	/* Single function older card. We read HDP_APER_CNTL to see how the BIOS
	 * have set it up. We don't write this as it's broken on some ASICs but
	 * we expect the BIOS to have done the right thing (might be too optimistic...)
	 */
	if (RREG32(RADEON_HOST_PATH_CNTL) & RADEON_HDP_APER_CNTL)
		return aper_size * 2;
	return aper_size;
}

void r100_vram_init_sizes(struct radeon_device *rdev)
{
	u64 config_aper_size;
	u32 accessible;

	config_aper_size = RREG32(RADEON_CONFIG_APER_SIZE);

	if (rdev->flags & RADEON_IS_IGP) {
		uint32_t tom;
		/* read NB_TOM to get the amount of ram stolen for the GPU */
		tom = RREG32(RADEON_NB_TOM);
		rdev->mc.real_vram_size = (((tom >> 16) - (tom & 0xffff) + 1) << 16);
		/* for IGPs we need to keep VRAM where it was put by the BIOS */
		rdev->mc.vram_location = (tom & 0xffff) << 16;
		WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
		rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	} else {
		rdev->mc.real_vram_size = RREG32(RADEON_CONFIG_MEMSIZE);
		/* Some production boards of m6 will report 0
		 * if it's 8 MB
		 */
		if (rdev->mc.real_vram_size == 0) {
			rdev->mc.real_vram_size = 8192 * 1024;
			WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
		}
		/* let driver place VRAM */
		rdev->mc.vram_location = 0xFFFFFFFFUL;
		 /* Fix for RN50, M6, M7 with 8/16/32(??) MBs of VRAM - 
		  * Novell bug 204882 + along with lots of ubuntu ones */
		if (config_aper_size > rdev->mc.real_vram_size)
			rdev->mc.mc_vram_size = config_aper_size;
		else
			rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	}

	/* work out accessible VRAM */
	accessible = r100_get_accessible_vram(rdev);

	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);

	if (accessible > rdev->mc.aper_size)
		accessible = rdev->mc.aper_size;

	if (rdev->mc.mc_vram_size > rdev->mc.aper_size)
		rdev->mc.mc_vram_size = rdev->mc.aper_size;

	if (rdev->mc.real_vram_size > rdev->mc.aper_size)
		rdev->mc.real_vram_size = rdev->mc.aper_size;
}

void r100_vram_info(struct radeon_device *rdev)
{
	r100_vram_get_type(rdev);

	r100_vram_init_sizes(rdev);
}


/*
 * Indirect registers accessor
 */
void r100_pll_errata_after_index(struct radeon_device *rdev)
{
	if (!(rdev->pll_errata & CHIP_ERRATA_PLL_DUMMYREADS)) {
		return;
	}
	(void)RREG32(RADEON_CLOCK_CNTL_DATA);
	(void)RREG32(RADEON_CRTC_GEN_CNTL);
}

static void r100_pll_errata_after_data(struct radeon_device *rdev)
{
	/* This workarounds is necessary on RV100, RS100 and RS200 chips
	 * or the chip could hang on a subsequent access
	 */
	if (rdev->pll_errata & CHIP_ERRATA_PLL_DELAY) {
		udelay(5000);
	}

	/* This function is required to workaround a hardware bug in some (all?)
	 * revisions of the R300.  This workaround should be called after every
	 * CLOCK_CNTL_INDEX register access.  If not, register reads afterward
	 * may not be correct.
	 */
	if (rdev->pll_errata & CHIP_ERRATA_R300_CG) {
		uint32_t save, tmp;

		save = RREG32(RADEON_CLOCK_CNTL_INDEX);
		tmp = save & ~(0x3f | RADEON_PLL_WR_EN);
		WREG32(RADEON_CLOCK_CNTL_INDEX, tmp);
		tmp = RREG32(RADEON_CLOCK_CNTL_DATA);
		WREG32(RADEON_CLOCK_CNTL_INDEX, save);
	}
}

uint32_t r100_pll_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t data;

	WREG8(RADEON_CLOCK_CNTL_INDEX, reg & 0x3f);
	r100_pll_errata_after_index(rdev);
	data = RREG32(RADEON_CLOCK_CNTL_DATA);
	r100_pll_errata_after_data(rdev);
	return data;
}

void r100_pll_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG8(RADEON_CLOCK_CNTL_INDEX, ((reg & 0x3f) | RADEON_PLL_WR_EN));
	r100_pll_errata_after_index(rdev);
	WREG32(RADEON_CLOCK_CNTL_DATA, v);
	r100_pll_errata_after_data(rdev);
}

uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg)
{
	if (reg < 0x10000)
		return readl(((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		return readl(((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}

void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	if (reg < 0x10000)
		writel(v, ((void __iomem *)rdev->rmmio) + reg);
	else {
		writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
		writel(v, ((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	}
}

int r100_init(struct radeon_device *rdev)
{
	return 0;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int r100_debugfs_rbbm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t reg, value;
	unsigned i;

	seq_printf(m, "RBBM_STATUS 0x%08x\n", RREG32(RADEON_RBBM_STATUS));
	seq_printf(m, "RBBM_CMDFIFO_STAT 0x%08x\n", RREG32(0xE7C));
	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
	for (i = 0; i < 64; i++) {
		WREG32(RADEON_RBBM_CMDFIFO_ADDR, i | 0x100);
		reg = (RREG32(RADEON_RBBM_CMDFIFO_DATA) - 1) >> 2;
		WREG32(RADEON_RBBM_CMDFIFO_ADDR, i);
		value = RREG32(RADEON_RBBM_CMDFIFO_DATA);
		seq_printf(m, "[0x%03X] 0x%04X=0x%08X\n", i, reg, value);
	}
	return 0;
}

static int r100_debugfs_cp_ring_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t rdp, wdp;
	unsigned count, i, j;

	radeon_ring_free_size(rdev);
	rdp = RREG32(RADEON_CP_RB_RPTR);
	wdp = RREG32(RADEON_CP_RB_WPTR);
	count = (rdp + rdev->cp.ring_size - wdp) & rdev->cp.ptr_mask;
	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
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


static int r100_debugfs_cp_csq_fifo(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t csq_stat, csq2_stat, tmp;
	unsigned r_rptr, r_wptr, ib1_rptr, ib1_wptr, ib2_rptr, ib2_wptr;
	unsigned i;

	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
	seq_printf(m, "CP_CSQ_MODE 0x%08x\n", RREG32(RADEON_CP_CSQ_MODE));
	csq_stat = RREG32(RADEON_CP_CSQ_STAT);
	csq2_stat = RREG32(RADEON_CP_CSQ2_STAT);
	r_rptr = (csq_stat >> 0) & 0x3ff;
	r_wptr = (csq_stat >> 10) & 0x3ff;
	ib1_rptr = (csq_stat >> 20) & 0x3ff;
	ib1_wptr = (csq2_stat >> 0) & 0x3ff;
	ib2_rptr = (csq2_stat >> 10) & 0x3ff;
	ib2_wptr = (csq2_stat >> 20) & 0x3ff;
	seq_printf(m, "CP_CSQ_STAT 0x%08x\n", csq_stat);
	seq_printf(m, "CP_CSQ2_STAT 0x%08x\n", csq2_stat);
	seq_printf(m, "Ring rptr %u\n", r_rptr);
	seq_printf(m, "Ring wptr %u\n", r_wptr);
	seq_printf(m, "Indirect1 rptr %u\n", ib1_rptr);
	seq_printf(m, "Indirect1 wptr %u\n", ib1_wptr);
	seq_printf(m, "Indirect2 rptr %u\n", ib2_rptr);
	seq_printf(m, "Indirect2 wptr %u\n", ib2_wptr);
	/* FIXME: 0, 128, 640 depends on fifo setup see cp_init_kms
	 * 128 = indirect1_start * 8 & 640 = indirect2_start * 8 */
	seq_printf(m, "Ring fifo:\n");
	for (i = 0; i < 256; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "rfifo[%04d]=0x%08X\n", i, tmp);
	}
	seq_printf(m, "Indirect1 fifo:\n");
	for (i = 256; i <= 512; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "ib1fifo[%04d]=0x%08X\n", i, tmp);
	}
	seq_printf(m, "Indirect2 fifo:\n");
	for (i = 640; i < ib1_wptr; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "ib2fifo[%04d]=0x%08X\n", i, tmp);
	}
	return 0;
}

static int r100_debugfs_mc_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(RADEON_CONFIG_MEMSIZE);
	seq_printf(m, "CONFIG_MEMSIZE 0x%08x\n", tmp);
	tmp = RREG32(RADEON_MC_FB_LOCATION);
	seq_printf(m, "MC_FB_LOCATION 0x%08x\n", tmp);
	tmp = RREG32(RADEON_BUS_CNTL);
	seq_printf(m, "BUS_CNTL 0x%08x\n", tmp);
	tmp = RREG32(RADEON_MC_AGP_LOCATION);
	seq_printf(m, "MC_AGP_LOCATION 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AGP_BASE);
	seq_printf(m, "AGP_BASE 0x%08x\n", tmp);
	tmp = RREG32(RADEON_HOST_PATH_CNTL);
	seq_printf(m, "HOST_PATH_CNTL 0x%08x\n", tmp);
	tmp = RREG32(0x01D0);
	seq_printf(m, "AIC_CTRL 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AIC_LO_ADDR);
	seq_printf(m, "AIC_LO_ADDR 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AIC_HI_ADDR);
	seq_printf(m, "AIC_HI_ADDR 0x%08x\n", tmp);
	tmp = RREG32(0x01E4);
	seq_printf(m, "AIC_TLB_ADDR 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list r100_debugfs_rbbm_list[] = {
	{"r100_rbbm_info", r100_debugfs_rbbm_info, 0, NULL},
};

static struct drm_info_list r100_debugfs_cp_list[] = {
	{"r100_cp_ring_info", r100_debugfs_cp_ring_info, 0, NULL},
	{"r100_cp_csq_fifo", r100_debugfs_cp_csq_fifo, 0, NULL},
};

static struct drm_info_list r100_debugfs_mc_info_list[] = {
	{"r100_mc_info", r100_debugfs_mc_info, 0, NULL},
};
#endif

int r100_debugfs_rbbm_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_rbbm_list, 1);
#else
	return 0;
#endif
}

int r100_debugfs_cp_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_cp_list, 2);
#else
	return 0;
#endif
}

int r100_debugfs_mc_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_mc_info_list, 1);
#else
	return 0;
#endif
}

int r100_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size)
{
	int surf_index = reg * 16;
	int flags = 0;

	/* r100/r200 divide by 16 */
	if (rdev->family < CHIP_R300)
		flags = pitch / 16;
	else
		flags = pitch / 8;

	if (rdev->family <= CHIP_RS200) {
		if ((tiling_flags & (RADEON_TILING_MACRO|RADEON_TILING_MICRO))
				 == (RADEON_TILING_MACRO|RADEON_TILING_MICRO))
			flags |= RADEON_SURF_TILE_COLOR_BOTH;
		if (tiling_flags & RADEON_TILING_MACRO)
			flags |= RADEON_SURF_TILE_COLOR_MACRO;
	} else if (rdev->family <= CHIP_RV280) {
		if (tiling_flags & (RADEON_TILING_MACRO))
			flags |= R200_SURF_TILE_COLOR_MACRO;
		if (tiling_flags & RADEON_TILING_MICRO)
			flags |= R200_SURF_TILE_COLOR_MICRO;
	} else {
		if (tiling_flags & RADEON_TILING_MACRO)
			flags |= R300_SURF_TILE_MACRO;
		if (tiling_flags & RADEON_TILING_MICRO)
			flags |= R300_SURF_TILE_MICRO;
	}

	DRM_DEBUG("writing surface %d %d %x %x\n", reg, flags, offset, offset+obj_size-1);
	WREG32(RADEON_SURFACE0_INFO + surf_index, flags);
	WREG32(RADEON_SURFACE0_LOWER_BOUND + surf_index, offset);
	WREG32(RADEON_SURFACE0_UPPER_BOUND + surf_index, offset + obj_size - 1);
	return 0;
}

void r100_clear_surface_reg(struct radeon_device *rdev, int reg)
{
	int surf_index = reg * 16;
	WREG32(RADEON_SURFACE0_INFO + surf_index, 0);
}

void r100_bandwidth_update(struct radeon_device *rdev)
{
	fixed20_12 trcd_ff, trp_ff, tras_ff, trbs_ff, tcas_ff;
	fixed20_12 sclk_ff, mclk_ff, sclk_eff_ff, sclk_delay_ff;
	fixed20_12 peak_disp_bw, mem_bw, pix_clk, pix_clk2, temp_ff, crit_point_ff;
	uint32_t temp, data, mem_trcd, mem_trp, mem_tras;
	fixed20_12 memtcas_ff[8] = {
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(0),
		fixed_init_half(1),
		fixed_init_half(2),
		fixed_init(0),
	};
	fixed20_12 memtcas_rs480_ff[8] = {
		fixed_init(0),
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(0),
		fixed_init_half(1),
		fixed_init_half(2),
		fixed_init_half(3),
	};
	fixed20_12 memtcas2_ff[8] = {
		fixed_init(0),
		fixed_init(1),
		fixed_init(2),
		fixed_init(3),
		fixed_init(4),
		fixed_init(5),
		fixed_init(6),
		fixed_init(7),
	};
	fixed20_12 memtrbs[8] = {
		fixed_init(1),
		fixed_init_half(1),
		fixed_init(2),
		fixed_init_half(2),
		fixed_init(3),
		fixed_init_half(3),
		fixed_init(4),
		fixed_init_half(4)
	};
	fixed20_12 memtrbs_r4xx[8] = {
		fixed_init(4),
		fixed_init(5),
		fixed_init(6),
		fixed_init(7),
		fixed_init(8),
		fixed_init(9),
		fixed_init(10),
		fixed_init(11)
	};
	fixed20_12 min_mem_eff;
	fixed20_12 mc_latency_sclk, mc_latency_mclk, k1;
	fixed20_12 cur_latency_mclk, cur_latency_sclk;
	fixed20_12 disp_latency, disp_latency_overhead, disp_drain_rate,
		disp_drain_rate2, read_return_rate;
	fixed20_12 time_disp1_drop_priority;
	int c;
	int cur_size = 16;       /* in octawords */
	int critical_point = 0, critical_point2;
/* 	uint32_t read_return_rate, time_disp1_drop_priority; */
	int stop_req, max_stop_req;
	struct drm_display_mode *mode1 = NULL;
	struct drm_display_mode *mode2 = NULL;
	uint32_t pixel_bytes1 = 0;
	uint32_t pixel_bytes2 = 0;

	if (rdev->mode_info.crtcs[0]->base.enabled) {
		mode1 = &rdev->mode_info.crtcs[0]->base.mode;
		pixel_bytes1 = rdev->mode_info.crtcs[0]->base.fb->bits_per_pixel / 8;
	}
	if (rdev->mode_info.crtcs[1]->base.enabled) {
		mode2 = &rdev->mode_info.crtcs[1]->base.mode;
		pixel_bytes2 = rdev->mode_info.crtcs[1]->base.fb->bits_per_pixel / 8;
	}

	min_mem_eff.full = rfixed_const_8(0);
	/* get modes */
	if ((rdev->disp_priority == 2) && ASIC_IS_R300(rdev)) {
		uint32_t mc_init_misc_lat_timer = RREG32(R300_MC_INIT_MISC_LAT_TIMER);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP1R_INIT_LAT_MASK << R300_MC_DISP1R_INIT_LAT_SHIFT);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP0R_INIT_LAT_MASK << R300_MC_DISP0R_INIT_LAT_SHIFT);
		/* check crtc enables */
		if (mode2)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP1R_INIT_LAT_SHIFT);
		if (mode1)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP0R_INIT_LAT_SHIFT);
		WREG32(R300_MC_INIT_MISC_LAT_TIMER, mc_init_misc_lat_timer);
	}

	/*
	 * determine is there is enough bw for current mode
	 */
	mclk_ff.full = rfixed_const(rdev->clock.default_mclk);
	temp_ff.full = rfixed_const(100);
	mclk_ff.full = rfixed_div(mclk_ff, temp_ff);
	sclk_ff.full = rfixed_const(rdev->clock.default_sclk);
	sclk_ff.full = rfixed_div(sclk_ff, temp_ff);

	temp = (rdev->mc.vram_width / 8) * (rdev->mc.vram_is_ddr ? 2 : 1);
	temp_ff.full = rfixed_const(temp);
	mem_bw.full = rfixed_mul(mclk_ff, temp_ff);

	pix_clk.full = 0;
	pix_clk2.full = 0;
	peak_disp_bw.full = 0;
	if (mode1) {
		temp_ff.full = rfixed_const(1000);
		pix_clk.full = rfixed_const(mode1->clock); /* convert to fixed point */
		pix_clk.full = rfixed_div(pix_clk, temp_ff);
		temp_ff.full = rfixed_const(pixel_bytes1);
		peak_disp_bw.full += rfixed_mul(pix_clk, temp_ff);
	}
	if (mode2) {
		temp_ff.full = rfixed_const(1000);
		pix_clk2.full = rfixed_const(mode2->clock); /* convert to fixed point */
		pix_clk2.full = rfixed_div(pix_clk2, temp_ff);
		temp_ff.full = rfixed_const(pixel_bytes2);
		peak_disp_bw.full += rfixed_mul(pix_clk2, temp_ff);
	}

	mem_bw.full = rfixed_mul(mem_bw, min_mem_eff);
	if (peak_disp_bw.full >= mem_bw.full) {
		DRM_ERROR("You may not have enough display bandwidth for current mode\n"
			  "If you have flickering problem, try to lower resolution, refresh rate, or color depth\n");
	}

	/*  Get values from the EXT_MEM_CNTL register...converting its contents. */
	temp = RREG32(RADEON_MEM_TIMING_CNTL);
	if ((rdev->family == CHIP_RV100) || (rdev->flags & RADEON_IS_IGP)) { /* RV100, M6, IGPs */
		mem_trcd = ((temp >> 2) & 0x3) + 1;
		mem_trp  = ((temp & 0x3)) + 1;
		mem_tras = ((temp & 0x70) >> 4) + 1;
	} else if (rdev->family == CHIP_R300 ||
		   rdev->family == CHIP_R350) { /* r300, r350 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 11) & 0xf) + 4;
	} else if (rdev->family == CHIP_RV350 ||
		   rdev->family <= CHIP_RV380) {
		/* rv3x0 */
		mem_trcd = (temp & 0x7) + 3;
		mem_trp = ((temp >> 8) & 0x7) + 3;
		mem_tras = ((temp >> 11) & 0xf) + 6;
	} else if (rdev->family == CHIP_R420 ||
		   rdev->family == CHIP_R423 ||
		   rdev->family == CHIP_RV410) {
		/* r4xx */
		mem_trcd = (temp & 0xf) + 3;
		if (mem_trcd > 15)
			mem_trcd = 15;
		mem_trp = ((temp >> 8) & 0xf) + 3;
		if (mem_trp > 15)
			mem_trp = 15;
		mem_tras = ((temp >> 12) & 0x1f) + 6;
		if (mem_tras > 31)
			mem_tras = 31;
	} else { /* RV200, R200 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 12) & 0xf) + 4;
	}
	/* convert to FF */
	trcd_ff.full = rfixed_const(mem_trcd);
	trp_ff.full = rfixed_const(mem_trp);
	tras_ff.full = rfixed_const(mem_tras);

	/* Get values from the MEM_SDRAM_MODE_REG register...converting its */
	temp = RREG32(RADEON_MEM_SDRAM_MODE_REG);
	data = (temp & (7 << 20)) >> 20;
	if ((rdev->family == CHIP_RV100) || rdev->flags & RADEON_IS_IGP) {
		if (rdev->family == CHIP_RS480) /* don't think rs400 */
			tcas_ff = memtcas_rs480_ff[data];
		else
			tcas_ff = memtcas_ff[data];
	} else
		tcas_ff = memtcas2_ff[data];

	if (rdev->family == CHIP_RS400 ||
	    rdev->family == CHIP_RS480) {
		/* extra cas latency stored in bits 23-25 0-4 clocks */
		data = (temp >> 23) & 0x7;
		if (data < 5)
			tcas_ff.full += rfixed_const(data);
	}

	if (ASIC_IS_R300(rdev) && !(rdev->flags & RADEON_IS_IGP)) {
		/* on the R300, Tcas is included in Trbs.
		 */
		temp = RREG32(RADEON_MEM_CNTL);
		data = (R300_MEM_NUM_CHANNELS_MASK & temp);
		if (data == 1) {
			if (R300_MEM_USE_CD_CH_ONLY & temp) {
				temp = RREG32(R300_MC_IND_INDEX);
				temp &= ~R300_MC_IND_ADDR_MASK;
				temp |= R300_MC_READ_CNTL_CD_mcind;
				WREG32(R300_MC_IND_INDEX, temp);
				temp = RREG32(R300_MC_IND_DATA);
				data = (R300_MEM_RBS_POSITION_C_MASK & temp);
			} else {
				temp = RREG32(R300_MC_READ_CNTL_AB);
				data = (R300_MEM_RBS_POSITION_A_MASK & temp);
			}
		} else {
			temp = RREG32(R300_MC_READ_CNTL_AB);
			data = (R300_MEM_RBS_POSITION_A_MASK & temp);
		}
		if (rdev->family == CHIP_RV410 ||
		    rdev->family == CHIP_R420 ||
		    rdev->family == CHIP_R423)
			trbs_ff = memtrbs_r4xx[data];
		else
			trbs_ff = memtrbs[data];
		tcas_ff.full += trbs_ff.full;
	}

	sclk_eff_ff.full = sclk_ff.full;

	if (rdev->flags & RADEON_IS_AGP) {
		fixed20_12 agpmode_ff;
		agpmode_ff.full = rfixed_const(radeon_agpmode);
		temp_ff.full = rfixed_const_666(16);
		sclk_eff_ff.full -= rfixed_mul(agpmode_ff, temp_ff);
	}
	/* TODO PCIE lanes may affect this - agpmode == 16?? */

	if (ASIC_IS_R300(rdev)) {
		sclk_delay_ff.full = rfixed_const(250);
	} else {
		if ((rdev->family == CHIP_RV100) ||
		    rdev->flags & RADEON_IS_IGP) {
			if (rdev->mc.vram_is_ddr)
				sclk_delay_ff.full = rfixed_const(41);
			else
				sclk_delay_ff.full = rfixed_const(33);
		} else {
			if (rdev->mc.vram_width == 128)
				sclk_delay_ff.full = rfixed_const(57);
			else
				sclk_delay_ff.full = rfixed_const(41);
		}
	}

	mc_latency_sclk.full = rfixed_div(sclk_delay_ff, sclk_eff_ff);

	if (rdev->mc.vram_is_ddr) {
		if (rdev->mc.vram_width == 32) {
			k1.full = rfixed_const(40);
			c  = 3;
		} else {
			k1.full = rfixed_const(20);
			c  = 1;
		}
	} else {
		k1.full = rfixed_const(40);
		c  = 3;
	}

	temp_ff.full = rfixed_const(2);
	mc_latency_mclk.full = rfixed_mul(trcd_ff, temp_ff);
	temp_ff.full = rfixed_const(c);
	mc_latency_mclk.full += rfixed_mul(tcas_ff, temp_ff);
	temp_ff.full = rfixed_const(4);
	mc_latency_mclk.full += rfixed_mul(tras_ff, temp_ff);
	mc_latency_mclk.full += rfixed_mul(trp_ff, temp_ff);
	mc_latency_mclk.full += k1.full;

	mc_latency_mclk.full = rfixed_div(mc_latency_mclk, mclk_ff);
	mc_latency_mclk.full += rfixed_div(temp_ff, sclk_eff_ff);

	/*
	  HW cursor time assuming worst case of full size colour cursor.
	*/
	temp_ff.full = rfixed_const((2 * (cur_size - (rdev->mc.vram_is_ddr + 1))));
	temp_ff.full += trcd_ff.full;
	if (temp_ff.full < tras_ff.full)
		temp_ff.full = tras_ff.full;
	cur_latency_mclk.full = rfixed_div(temp_ff, mclk_ff);

	temp_ff.full = rfixed_const(cur_size);
	cur_latency_sclk.full = rfixed_div(temp_ff, sclk_eff_ff);
	/*
	  Find the total latency for the display data.
	*/
	disp_latency_overhead.full = rfixed_const(80);
	disp_latency_overhead.full = rfixed_div(disp_latency_overhead, sclk_ff);
	mc_latency_mclk.full += disp_latency_overhead.full + cur_latency_mclk.full;
	mc_latency_sclk.full += disp_latency_overhead.full + cur_latency_sclk.full;

	if (mc_latency_mclk.full > mc_latency_sclk.full)
		disp_latency.full = mc_latency_mclk.full;
	else
		disp_latency.full = mc_latency_sclk.full;

	/* setup Max GRPH_STOP_REQ default value */
	if (ASIC_IS_RV100(rdev))
		max_stop_req = 0x5c;
	else
		max_stop_req = 0x7c;

	if (mode1) {
		/*  CRTC1
		    Set GRPH_BUFFER_CNTL register using h/w defined optimal values.
		    GRPH_STOP_REQ <= MIN[ 0x7C, (CRTC_H_DISP + 1) * (bit depth) / 0x10 ]
		*/
		stop_req = mode1->hdisplay * pixel_bytes1 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = rfixed_const((16/pixel_bytes1));
		disp_drain_rate.full = rfixed_div(pix_clk, temp_ff);

		/*
		  Find the critical point of the display buffer.
		*/
		crit_point_ff.full = rfixed_mul(disp_drain_rate, disp_latency);
		crit_point_ff.full += rfixed_const_half(0);

		critical_point = rfixed_trunc(crit_point_ff);

		if (rdev->disp_priority == 2) {
			critical_point = 0;
		}

		/*
		  The critical point should never be above max_stop_req-4.  Setting
		  GRPH_CRITICAL_CNTL = 0 will thus force high priority all the time.
		*/
		if (max_stop_req - critical_point < 4)
			critical_point = 0;

		if (critical_point == 0 && mode2 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0, when CRTC2 is enabled.*/
			critical_point = 0x10;
		}

		temp = RREG32(RADEON_GRPH_BUFFER_CNTL);
		temp &= ~(RADEON_GRPH_STOP_REQ_MASK);
		temp |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		temp &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		temp |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		temp |= RADEON_GRPH_BUFFER_SIZE;
		temp &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);
		/*
		  Write the result into the register.
		*/
		WREG32(RADEON_GRPH_BUFFER_CNTL, ((temp & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						       (critical_point << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

#if 0
		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
			/* attempt to program RS400 disp regs correctly ??? */
			temp = RREG32(RS400_DISP1_REG_CNTL);
			temp &= ~(RS400_DISP1_START_REQ_LEVEL_MASK |
				  RS400_DISP1_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP1_REQ_CNTL1, (temp |
						       (critical_point << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DMIF_MEM_CNTL1);
			temp &= ~(RS400_DISP1_CRITICAL_POINT_START_MASK |
				  RS400_DISP1_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DMIF_MEM_CNTL1, (temp |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_START_SHIFT) |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_STOP_SHIFT)));
		}
#endif

		DRM_DEBUG("GRPH_BUFFER_CNTL from to %x\n",
			  /* 	  (unsigned int)info->SavedReg->grph_buffer_cntl, */
			  (unsigned int)RREG32(RADEON_GRPH_BUFFER_CNTL));
	}

	if (mode2) {
		u32 grph2_cntl;
		stop_req = mode2->hdisplay * pixel_bytes2 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = rfixed_const((16/pixel_bytes2));
		disp_drain_rate2.full = rfixed_div(pix_clk2, temp_ff);

		grph2_cntl = RREG32(RADEON_GRPH2_BUFFER_CNTL);
		grph2_cntl &= ~(RADEON_GRPH_STOP_REQ_MASK);
		grph2_cntl |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		grph2_cntl &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		grph2_cntl |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		grph2_cntl |= RADEON_GRPH_BUFFER_SIZE;
		grph2_cntl &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);

		if ((rdev->family == CHIP_RS100) ||
		    (rdev->family == CHIP_RS200))
			critical_point2 = 0;
		else {
			temp = (rdev->mc.vram_width * rdev->mc.vram_is_ddr + 1)/128;
			temp_ff.full = rfixed_const(temp);
			temp_ff.full = rfixed_mul(mclk_ff, temp_ff);
			if (sclk_ff.full < temp_ff.full)
				temp_ff.full = sclk_ff.full;

			read_return_rate.full = temp_ff.full;

			if (mode1) {
				temp_ff.full = read_return_rate.full - disp_drain_rate.full;
				time_disp1_drop_priority.full = rfixed_div(crit_point_ff, temp_ff);
			} else {
				time_disp1_drop_priority.full = 0;
			}
			crit_point_ff.full = disp_latency.full + time_disp1_drop_priority.full + disp_latency.full;
			crit_point_ff.full = rfixed_mul(crit_point_ff, disp_drain_rate2);
			crit_point_ff.full += rfixed_const_half(0);

			critical_point2 = rfixed_trunc(crit_point_ff);

			if (rdev->disp_priority == 2) {
				critical_point2 = 0;
			}

			if (max_stop_req - critical_point2 < 4)
				critical_point2 = 0;

		}

		if (critical_point2 == 0 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0 */
			critical_point2 = 0x10;
		}

		WREG32(RADEON_GRPH2_BUFFER_CNTL, ((grph2_cntl & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						  (critical_point2 << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
#if 0
			/* attempt to program RS400 disp2 regs correctly ??? */
			temp = RREG32(RS400_DISP2_REQ_CNTL1);
			temp &= ~(RS400_DISP2_START_REQ_LEVEL_MASK |
				  RS400_DISP2_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP2_REQ_CNTL1, (temp |
						       (critical_point2 << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point2 << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DISP2_REQ_CNTL2);
			temp &= ~(RS400_DISP2_CRITICAL_POINT_START_MASK |
				  RS400_DISP2_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DISP2_REQ_CNTL2, (temp |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_START_SHIFT) |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_STOP_SHIFT)));
#endif
			WREG32(RS400_DISP2_REQ_CNTL1, 0x105DC1CC);
			WREG32(RS400_DISP2_REQ_CNTL2, 0x2749D000);
			WREG32(RS400_DMIF_MEM_CNTL1,  0x29CA71DC);
			WREG32(RS400_DISP1_REQ_CNTL1, 0x28FBC3AC);
		}

		DRM_DEBUG("GRPH2_BUFFER_CNTL from to %x\n",
			  (unsigned int)RREG32(RADEON_GRPH2_BUFFER_CNTL));
	}
}
