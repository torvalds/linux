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
#include "radeon_reg.h"
#include "radeon.h"

/* r420,r423,rv410 depends on : */
void r100_pci_gart_disable(struct radeon_device *rdev);
void r100_hdp_reset(struct radeon_device *rdev);
void r100_mc_setup(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
void r100_mc_disable_clients(struct radeon_device *rdev);
void r300_vram_info(struct radeon_device *rdev);
int r300_mc_wait_for_idle(struct radeon_device *rdev);
int rv370_pcie_gart_enable(struct radeon_device *rdev);
void rv370_pcie_gart_disable(struct radeon_device *rdev);

/* This files gather functions specifics to :
 * r420,r423,rv410
 *
 * Some of these functions might be used by newer ASICs.
 */
void r420_gpu_init(struct radeon_device *rdev);
int r420_debugfs_pipes_info_init(struct radeon_device *rdev);


/*
 * MC
 */
int r420_mc_init(struct radeon_device *rdev)
{
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}
	if (r420_debugfs_pipes_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for pipes !\n");
	}

	r420_gpu_init(rdev);
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

void r420_mc_fini(struct radeon_device *rdev)
{
	rv370_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}


/*
 * Global GPU functions
 */
void r420_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

void r420_pipes_init(struct radeon_device *rdev)
{
	unsigned tmp;
	unsigned gb_pipe_select;
	unsigned num_pipes;

	/* GA_ENHANCE workaround TCL deadlock issue */
	WREG32(0x4274, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
	/* get max number of pipes */
	gb_pipe_select = RREG32(0x402C);
	num_pipes = ((gb_pipe_select >> 12) & 3) + 1;
	rdev->num_gb_pipes = num_pipes;
	tmp = 0;
	switch (num_pipes) {
	default:
		/* force to 1 pipe */
		num_pipes = 1;
	case 1:
		tmp = (0 << 1);
		break;
	case 2:
		tmp = (3 << 1);
		break;
	case 3:
		tmp = (6 << 1);
		break;
	case 4:
		tmp = (7 << 1);
		break;
	}
	WREG32(0x42C8, (1 << num_pipes) - 1);
	/* Sub pixel 1/12 so we can have 4K rendering according to doc */
	tmp |= (1 << 4) | (1 << 0);
	WREG32(0x4018, tmp);
	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = RREG32(0x170C);
	WREG32(0x170C, tmp | (1 << 31));

	WREG32(R300_RB2D_DSTCACHE_MODE,
	       RREG32(R300_RB2D_DSTCACHE_MODE) |
	       R300_DC_AUTOFLUSH_ENABLE |
	       R300_DC_DC_DISABLE_IGNORE_PE);

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	DRM_INFO("radeon: %d pipes initialized.\n", rdev->num_gb_pipes);
}

void r420_gpu_init(struct radeon_device *rdev)
{
	r100_hdp_reset(rdev);
	r420_pipes_init(rdev);
	if (r300_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}


/*
 * r420,r423,rv410 VRAM info
 */
void r420_vram_info(struct radeon_device *rdev)
{
	r300_vram_info(rdev);
}


/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int r420_debugfs_pipes_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(R400_GB_PIPE_SELECT);
	seq_printf(m, "GB_PIPE_SELECT 0x%08x\n", tmp);
	tmp = RREG32(R300_GB_TILE_CONFIG);
	seq_printf(m, "GB_TILE_CONFIG 0x%08x\n", tmp);
	tmp = RREG32(R300_DST_PIPE_CONFIG);
	seq_printf(m, "DST_PIPE_CONFIG 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list r420_pipes_info_list[] = {
	{"r420_pipes_info", r420_debugfs_pipes_info, 0, NULL},
};
#endif

int r420_debugfs_pipes_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r420_pipes_info_list, 1);
#else
	return 0;
#endif
}
