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
#include "radeon_reg.h"
#include "radeon.h"

/* rs400,rs480 depends on : */
void r100_hdp_reset(struct radeon_device *rdev);
void r100_mc_disable_clients(struct radeon_device *rdev);
int r300_mc_wait_for_idle(struct radeon_device *rdev);
void r420_pipes_init(struct radeon_device *rdev);

/* This files gather functions specifics to :
 * rs400,rs480
 *
 * Some of these functions might be used by newer ASICs.
 */
void rs400_gpu_init(struct radeon_device *rdev);
int rs400_debugfs_pcie_gart_info_init(struct radeon_device *rdev);


/*
 * GART functions.
 */
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
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	tmp = REG_SET(RS690_MC_AGP_TOP, tmp >> 16);
	tmp |= REG_SET(RS690_MC_AGP_START, rdev->mc.gtt_location >> 16);
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


/*
 * MC functions.
 */
int rs400_mc_init(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}

	rs400_gpu_init(rdev);
	rs400_gart_disable(rdev);
	rdev->mc.gtt_location = rdev->mc.mc_vram_size;
	rdev->mc.gtt_location += (rdev->mc.gtt_size - 1);
	rdev->mc.gtt_location &= ~(rdev->mc.gtt_size - 1);
	r = radeon_mc_setup(rdev);
	if (r) {
		return r;
	}

	r100_mc_disable_clients(rdev);
	if (r300_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = rdev->mc.vram_location + rdev->mc.mc_vram_size - 1;
	tmp = REG_SET(RADEON_MC_FB_TOP, tmp >> 16);
	tmp |= REG_SET(RADEON_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32(RADEON_MC_FB_LOCATION, tmp);
	tmp = RREG32(RADEON_HOST_PATH_CNTL) | RADEON_HP_LIN_RD_CACHE_DIS;
	WREG32(RADEON_HOST_PATH_CNTL, tmp | RADEON_HDP_SOFT_RESET | RADEON_HDP_READ_BUFFER_INVALIDATE);
	(void)RREG32(RADEON_HOST_PATH_CNTL);
	WREG32(RADEON_HOST_PATH_CNTL, tmp);
	(void)RREG32(RADEON_HOST_PATH_CNTL);

	return 0;
}

void rs400_mc_fini(struct radeon_device *rdev)
{
}


/*
 * Global GPU functions
 */
void rs400_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

void rs400_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: HDP same place on rs400 ? */
	r100_hdp_reset(rdev);
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	if (r300_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}


/*
 * VRAM info.
 */
void rs400_vram_info(struct radeon_device *rdev)
{
	rs400_gart_adjust_size(rdev);
	/* DDR for all card after R300 & IGP */
	rdev->mc.vram_is_ddr = true;
	rdev->mc.vram_width = 128;

	r100_vram_init_sizes(rdev);
}


/*
 * Indirect registers accessor
 */
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


/*
 * Debugfs info
 */
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

int rs400_debugfs_pcie_gart_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rs400_gart_info_list, 1);
#else
	return 0;
#endif
}
