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
#include <drm/drmP.h>
#include "radeon.h"
#include "rs400d.h"

/* This files gather functions specifics to : rs400,rs480 */
static int rs400_debugfs_pcie_gart_info_init(struct radeon_device *rdev);

void rs400_gart_adjust_size(struct radeon_device *rdev)
{
	/* Check gart size */
	switch (rdev->mc.gtt_size/(1024*1024)) {
	case 32:
	case 64:
	case 128:
	case 256:
	case 512:
	case 1024:
	case 2048:
		break;
	default:
		DRM_ERROR("Unable to use IGP GART size %uM\n",
			  (unsigned)(rdev->mc.gtt_size >> 20));
		DRM_ERROR("Valid GART size for IGP are 32M,64M,128M,256M,512M,1G,2G\n");
		DRM_ERROR("Forcing to 32M GART size\n");
		rdev->mc.gtt_size = 32 * 1024 * 1024;
		return;
	}
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480) {
		/* FIXME: RS400 & RS480 seems to have issue with GART size
		 * if 4G of system memory (needs more testing) */
		rdev->mc.gtt_size = 32 * 1024 * 1024;
		DRM_ERROR("Forcing to 32M GART size (because of ASIC bug ?)\n");
	}
}

void rs400_gart_tlb_flush(struct radeon_device *rdev)
{
	uint32_t tmp;
	unsigned int timeout = rdev->usec_timeout;

	WREG32_MC(RS480_GART_CACHE_CNTRL, RS480_GART_CACHE_INVALIDATE);
	do {
		tmp = RREG32_MC(RS480_GART_CACHE_CNTRL);
		if ((tmp & RS480_GART_CACHE_INVALIDATE) == 0)
			break;
		DRM_UDELAY(1);
		timeout--;
	} while (timeout > 0);
	WREG32_MC(RS480_GART_CACHE_CNTRL, 0);
}

int rs400_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.table.ram.ptr) {
		WARN(1, "RS400 GART already initialized.\n");
		return 0;
	}
	/* Check gart size */
	switch(rdev->mc.gtt_size / (1024 * 1024)) {
	case 32:
	case 64:
	case 128:
	case 256:
	case 512:
	case 1024:
	case 2048:
		break;
	default:
		return -EINVAL;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r)
		return r;
	if (rs400_debugfs_pcie_gart_info_init(rdev))
		DRM_ERROR("Failed to register debugfs file for RS400 GART !\n");
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 4;
	return radeon_gart_table_ram_alloc(rdev);
}

int rs400_gart_enable(struct radeon_device *rdev)
{
	uint32_t size_reg;
	uint32_t tmp;

	radeon_gart_restore(rdev);
	tmp = RREG32_MC(RS690_AIC_CTRL_SCRATCH);
	tmp |= RS690_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32_MC(RS690_AIC_CTRL_SCRATCH, tmp);
	/* Check gart size */
	switch(rdev->mc.gtt_size / (1024 * 1024)) {
	case 32:
		size_reg = RS480_VA_SIZE_32MB;
		break;
	case 64:
		size_reg = RS480_VA_SIZE_64MB;
		break;
	case 128:
		size_reg = RS480_VA_SIZE_128MB;
		break;
	case 256:
		size_reg = RS480_VA_SIZE_256MB;
		break;
	case 512:
		size_reg = RS480_VA_SIZE_512MB;
		break;
	case 1024:
		size_reg = RS480_VA_SIZE_1GB;
		break;
	case 2048:
		size_reg = RS480_VA_SIZE_2GB;
		break;
	default:
		return -EINVAL;
	}
	/* It should be fine to program it to max value */
	if (rdev->family == CHIP_RS690 || (rdev->family == CHIP_RS740)) {
		WREG32_MC(RS690_MCCFG_AGP_BASE, 0xFFFFFFFF);
		WREG32_MC(RS690_MCCFG_AGP_BASE_2, 0);
	} else {
		WREG32(RADEON_AGP_BASE, 0xFFFFFFFF);
		WREG32(RS480_AGP_BASE_2, 0);
	}
	tmp = REG_SET(RS690_MC_AGP_TOP, rdev->mc.gtt_end >> 16);
	tmp |= REG_SET(RS690_MC_AGP_START, rdev->mc.gtt_start >> 16);
	if ((rdev->family == CHIP_RS690) || (rdev->family == CHIP_RS740)) {
		WREG32_MC(RS690_MCCFG_AGP_LOCATION, tmp);
		tmp = RREG32(RADEON_BUS_CNTL) & ~RS600_BUS_MASTER_DIS;
		WREG32(RADEON_BUS_CNTL, tmp);
	} else {
		WREG32(RADEON_MC_AGP_LOCATION, tmp);
		tmp = RREG32(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
		WREG32(RADEON_BUS_CNTL, tmp);
	}
	/* Table should be in 32bits address space so ignore bits above. */
	tmp = (u32)rdev->gart.table_addr & 0xfffff000;
	tmp |= (upper_32_bits(rdev->gart.table_addr) & 0xff) << 4;

	WREG32_MC(RS480_GART_BASE, tmp);
	/* TODO: more tweaking here */
	WREG32_MC(RS480_GART_FEATURE_ID,
		  (RS480_TLB_ENABLE |
		   RS480_GTW_LAC_EN | RS480_1LEVEL_GART));
	/* Disable snooping */
	WREG32_MC(RS480_AGP_MODE_CNTL,
		  (1 << RS480_REQ_TYPE_SNOOP_SHIFT) | RS480_REQ_TYPE_SNOOP_DIS);
	/* Disable AGP mode */
	/* FIXME: according to doc we should set HIDE_MMCFG_BAR=0,
	 * AGPMODE30=0 & AGP30ENHANCED=0 in NB_CNTL */
	if ((rdev->family == CHIP_RS690) || (rdev->family == CHIP_RS740)) {
		WREG32_MC(RS480_MC_MISC_CNTL,
			  (RS480_GART_INDEX_REG_EN | RS690_BLOCK_GFX_D3_EN));
	} else {
		WREG32_MC(RS480_MC_MISC_CNTL, RS480_GART_INDEX_REG_EN);
	}
	/* Enable gart */
	WREG32_MC(RS480_AGP_ADDRESS_SPACE_SIZE, (RS480_GART_EN | size_reg));
	rs400_gart_tlb_flush(rdev);
	rdev->gart.ready = true;
	return 0;
}

void rs400_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32_MC(RS690_AIC_CTRL_SCRATCH);
	tmp |= RS690_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32_MC(RS690_AIC_CTRL_SCRATCH, tmp);
	WREG32_MC(RS480_AGP_ADDRESS_SPACE_SIZE, 0);
}

void rs400_gart_fini(struct radeon_device *rdev)
{
	rs400_gart_disable(rdev);
	radeon_gart_table_ram_free(rdev);
	radeon_gart_fini(rdev);
}

int rs400_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	uint32_t entry;

	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}

	entry = (lower_32_bits(addr) & PAGE_MASK) |
		((upper_32_bits(addr) & 0xff) << 4) |
		0xc;
	entry = cpu_to_le32(entry);
	rdev->gart.table.ram.ptr[i] = entry;
	return 0;
}

int rs400_mc_wait_for_idle(struct radeon_device *rdev)
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

void rs400_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: HDP same place on rs400 ? */
	r100_hdp_reset(rdev);
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	if (rs400_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "rs400: Failed to wait MC idle while "
		       "programming pipes. Bad things might happen. %08x\n", RREG32(0x150));
	}
}

void rs400_mc_init(struct radeon_device *rdev)
{
	u64 base;

	rs400_gart_adjust_size(rdev);
	rdev->mc.igp_sideport_enabled = radeon_combios_sideport_present(rdev);
	/* DDR for all card after R300 & IGP */
	rdev->mc.vram_is_ddr = true;
	rdev->mc.vram_width = 128;
	r100_vram_init_sizes(rdev);
	base = (RREG32(RADEON_NB_TOM) & 0xffff) << 16;
	radeon_vram_location(rdev, &rdev->mc, base);
	radeon_gtt_location(rdev, &rdev->mc);
}

uint32_t rs400_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(RS480_NB_MC_INDEX, reg & 0xff);
	r = RREG32(RS480_NB_MC_DATA);
	WREG32(RS480_NB_MC_INDEX, 0xff);
	return r;
}

void rs400_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(RS480_NB_MC_INDEX, ((reg) & 0xff) | RS480_NB_MC_IND_WR_EN);
	WREG32(RS480_NB_MC_DATA, (v));
	WREG32(RS480_NB_MC_INDEX, 0xff);
}

#if defined(CONFIG_DEBUG_FS)
static int rs400_debugfs_gart_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(RADEON_HOST_PATH_CNTL);
	seq_printf(m, "HOST_PATH_CNTL 0x%08x\n", tmp);
	tmp = RREG32(RADEON_BUS_CNTL);
	seq_printf(m, "BUS_CNTL 0x%08x\n", tmp);
	tmp = RREG32_MC(RS690_AIC_CTRL_SCRATCH);
	seq_printf(m, "AIC_CTRL_SCRATCH 0x%08x\n", tmp);
	if (rdev->family == CHIP_RS690 || (rdev->family == CHIP_RS740)) {
		tmp = RREG32_MC(RS690_MCCFG_AGP_BASE);
		seq_printf(m, "MCCFG_AGP_BASE 0x%08x\n", tmp);
		tmp = RREG32_MC(RS690_MCCFG_AGP_BASE_2);
		seq_printf(m, "MCCFG_AGP_BASE_2 0x%08x\n", tmp);
		tmp = RREG32_MC(RS690_MCCFG_AGP_LOCATION);
		seq_printf(m, "MCCFG_AGP_LOCATION 0x%08x\n", tmp);
		tmp = RREG32_MC(0x100);
		seq_printf(m, "MCCFG_FB_LOCATION 0x%08x\n", tmp);
		tmp = RREG32(0x134);
		seq_printf(m, "HDP_FB_LOCATION 0x%08x\n", tmp);
	} else {
		tmp = RREG32(RADEON_AGP_BASE);
		seq_printf(m, "AGP_BASE 0x%08x\n", tmp);
		tmp = RREG32(RS480_AGP_BASE_2);
		seq_printf(m, "AGP_BASE_2 0x%08x\n", tmp);
		tmp = RREG32(RADEON_MC_AGP_LOCATION);
		seq_printf(m, "MC_AGP_LOCATION 0x%08x\n", tmp);
	}
	tmp = RREG32_MC(RS480_GART_BASE);
	seq_printf(m, "GART_BASE 0x%08x\n", tmp);
	tmp = RREG32_MC(RS480_GART_FEATURE_ID);
	seq_printf(m, "GART_FEATURE_ID 0x%08x\n", tmp);
	tmp = RREG32_MC(RS480_AGP_MODE_CNTL);
	seq_printf(m, "AGP_MODE_CONTROL 0x%08x\n", tmp);
	tmp = RREG32_MC(RS480_MC_MISC_CNTL);
	seq_printf(m, "MC_MISC_CNTL 0x%08x\n", tmp);
	tmp = RREG32_MC(0x5F);
	seq_printf(m, "MC_MISC_UMA_CNTL 0x%08x\n", tmp);
	tmp = RREG32_MC(RS480_AGP_ADDRESS_SPACE_SIZE);
	seq_printf(m, "AGP_ADDRESS_SPACE_SIZE 0x%08x\n", tmp);
	tmp = RREG32_MC(RS480_GART_CACHE_CNTRL);
	seq_printf(m, "GART_CACHE_CNTRL 0x%08x\n", tmp);
	tmp = RREG32_MC(0x3B);
	seq_printf(m, "MC_GART_ERROR_ADDRESS 0x%08x\n", tmp);
	tmp = RREG32_MC(0x3C);
	seq_printf(m, "MC_GART_ERROR_ADDRESS_HI 0x%08x\n", tmp);
	tmp = RREG32_MC(0x30);
	seq_printf(m, "GART_ERROR_0 0x%08x\n", tmp);
	tmp = RREG32_MC(0x31);
	seq_printf(m, "GART_ERROR_1 0x%08x\n", tmp);
	tmp = RREG32_MC(0x32);
	seq_printf(m, "GART_ERROR_2 0x%08x\n", tmp);
	tmp = RREG32_MC(0x33);
	seq_printf(m, "GART_ERROR_3 0x%08x\n", tmp);
	tmp = RREG32_MC(0x34);
	seq_printf(m, "GART_ERROR_4 0x%08x\n", tmp);
	tmp = RREG32_MC(0x35);
	seq_printf(m, "GART_ERROR_5 0x%08x\n", tmp);
	tmp = RREG32_MC(0x36);
	seq_printf(m, "GART_ERROR_6 0x%08x\n", tmp);
	tmp = RREG32_MC(0x37);
	seq_printf(m, "GART_ERROR_7 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list rs400_gart_info_list[] = {
	{"rs400_gart_info", rs400_debugfs_gart_info, 0, NULL},
};
#endif

static int rs400_debugfs_pcie_gart_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rs400_gart_info_list, 1);
#else
	return 0;
#endif
}

void rs400_mc_program(struct radeon_device *rdev)
{
	struct r100_mc_save save;

	/* Stops all mc clients */
	r100_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (rs400_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "rs400: Wait MC idle timeout before updating MC.\n");
	WREG32(R_000148_MC_FB_LOCATION,
		S_000148_MC_FB_START(rdev->mc.vram_start >> 16) |
		S_000148_MC_FB_TOP(rdev->mc.vram_end >> 16));

	r100_mc_resume(rdev, &save);
}

static int rs400_startup(struct radeon_device *rdev)
{
	int r;

	rs400_mc_program(rdev);
	/* Resume clock */
	r300_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	rs400_gpu_init(rdev);
	r100_enable_bm(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r = rs400_gart_enable(rdev);
	if (r)
		return r;
	/* Enable IRQ */
	r100_irq_set(rdev);
	rdev->config.r300.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
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

int rs400_resume(struct radeon_device *rdev)
{
	/* Make sur GART are not working */
	rs400_gart_disable(rdev);
	/* Resume clock before doing reset */
	r300_clock_startup(rdev);
	/* setup MC before calling post tables */
	rs400_mc_program(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
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
	return rs400_startup(rdev);
}

int rs400_suspend(struct radeon_device *rdev)
{
	r100_cp_disable(rdev);
	r100_wb_disable(rdev);
	r100_irq_disable(rdev);
	rs400_gart_disable(rdev);
	return 0;
}

void rs400_fini(struct radeon_device *rdev)
{
	r100_cp_fini(rdev);
	r100_wb_fini(rdev);
	r100_ib_fini(rdev);
	radeon_gem_fini(rdev);
	rs400_gart_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int rs400_init(struct radeon_device *rdev)
{
	int r;

	/* Disable VGA */
	r100_vga_render_disable(rdev);
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
		dev_err(rdev->dev, "Expecting combios for RS400/RS480 GPU\n");
		return -EINVAL;
	} else {
		r = radeon_combios_init(rdev);
		if (r)
			return r;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (radeon_boot_test_post_card(rdev) == false)
		return -EINVAL;

	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Initialize power management */
	radeon_pm_init(rdev);
	/* initialize memory controller */
	rs400_mc_init(rdev);
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
	r = rs400_gart_init(rdev);
	if (r)
		return r;
	r300_set_reg_safe(rdev);
	rdev->accel_working = true;
	r = rs400_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		r100_wb_fini(rdev);
		r100_ib_fini(rdev);
		rs400_gart_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
