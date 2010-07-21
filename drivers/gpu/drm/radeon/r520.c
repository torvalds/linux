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
#include "radeon.h"
#include "radeon_asic.h"
#include "atom.h"
#include "r520d.h"

/* This files gather functions specifics to: r520,rv530,rv560,rv570,r580 */

static int r520_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(R520_MC_STATUS);
		if (tmp & R520_MC_STATUS_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

static void r520_gpu_init(struct radeon_device *rdev)
{
	unsigned pipe_select_current, gb_pipe_select, tmp;

	rv515_vga_render_disable(rdev);
	/*
	 * DST_PIPE_CONFIG		0x170C
	 * GB_TILE_CONFIG		0x4018
	 * GB_FIFO_SIZE			0x4024
	 * GB_PIPE_SELECT		0x402C
	 * GB_PIPE_SELECT2              0x4124
	 *	Z_PIPE_SHIFT			0
	 *	Z_PIPE_MASK			0x000000003
	 * GB_FIFO_SIZE2                0x4128
	 *	SC_SFIFO_SIZE_SHIFT		0
	 *	SC_SFIFO_SIZE_MASK		0x000000003
	 *	SC_MFIFO_SIZE_SHIFT		2
	 *	SC_MFIFO_SIZE_MASK		0x00000000C
	 *	FG_SFIFO_SIZE_SHIFT		4
	 *	FG_SFIFO_SIZE_MASK		0x000000030
	 *	ZB_MFIFO_SIZE_SHIFT		6
	 *	ZB_MFIFO_SIZE_MASK		0x0000000C0
	 * GA_ENHANCE			0x4274
	 * SU_REG_DEST			0x42C8
	 */
	/* workaround for RV530 */
	if (rdev->family == CHIP_RV530) {
		WREG32(0x4128, 0xFF);
	}
	r420_pipes_init(rdev);
	gb_pipe_select = RREG32(0x402C);
	tmp = RREG32(0x170C);
	pipe_select_current = (tmp >> 2) & 3;
	tmp = (1 << pipe_select_current) |
	      (((gb_pipe_select >> 8) & 0xF) << 4);
	WREG32_PLL(0x000D, tmp);
	if (r520_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}

static void r520_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;

	rdev->mc.vram_width = 128;
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32_MC(R520_MC_CNTL0);
	switch ((tmp & R520_MEM_NUM_CHANNELS_MASK) >> R520_MEM_NUM_CHANNELS_SHIFT) {
	case 0:
		rdev->mc.vram_width = 32;
		break;
	case 1:
		rdev->mc.vram_width = 64;
		break;
	case 2:
		rdev->mc.vram_width = 128;
		break;
	case 3:
		rdev->mc.vram_width = 256;
		break;
	default:
		rdev->mc.vram_width = 128;
		break;
	}
	if (tmp & R520_MC_CHANNEL_SIZE)
		rdev->mc.vram_width *= 2;
}

void r520_mc_init(struct radeon_device *rdev)
{

	r520_vram_get_type(rdev);
	r100_vram_init_sizes(rdev);
	radeon_vram_location(rdev, &rdev->mc, 0);
	rdev->mc.gtt_base_align = 0;
	if (!(rdev->flags & RADEON_IS_AGP))
		radeon_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);
}

void r520_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;

	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (r520_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");
	/* Write VRAM size in case we are limiting it */
	WREG32(R_0000F8_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
	/* Program MC, should be a 32bits limited address space */
	WREG32_MC(R_000004_MC_FB_LOCATION,
			S_000004_MC_FB_START(rdev->mc.vram_start >> 16) |
			S_000004_MC_FB_TOP(rdev->mc.vram_end >> 16));
	WREG32(R_000134_HDP_FB_LOCATION,
		S_000134_HDP_FB_START(rdev->mc.vram_start >> 16));
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32_MC(R_000005_MC_AGP_LOCATION,
			S_000005_MC_AGP_START(rdev->mc.gtt_start >> 16) |
			S_000005_MC_AGP_TOP(rdev->mc.gtt_end >> 16));
		WREG32_MC(R_000006_AGP_BASE, lower_32_bits(rdev->mc.agp_base));
		WREG32_MC(R_000007_AGP_BASE_2,
			S_000007_AGP_BASE_ADDR_2(upper_32_bits(rdev->mc.agp_base)));
	} else {
		WREG32_MC(R_000005_MC_AGP_LOCATION, 0xFFFFFFFF);
		WREG32_MC(R_000006_AGP_BASE, 0);
		WREG32_MC(R_000007_AGP_BASE_2, 0);
	}

	rv515_mc_resume(rdev, &save);
}

static int r520_startup(struct radeon_device *rdev)
{
	int r;

	r520_mc_program(rdev);
	/* Resume clock */
	rv515_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	r520_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_enable(rdev);
		if (r)
			return r;
	}
	/* Enable IRQ */
	rs600_irq_set(rdev);
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

int r520_resume(struct radeon_device *rdev)
{
	/* Make sur GART are not working */
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	/* Resume clock before doing reset */
	rv515_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Resume clock after posting */
	rv515_clock_startup(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	return r520_startup(rdev);
}

int r520_init(struct radeon_device *rdev)
{
	int r;

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
		r = radeon_atombios_init(rdev);
		if (r)
			return r;
	} else {
		dev_err(rdev->dev, "Expecting atombios for RV515 GPU\n");
		return -EINVAL;
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

	if (!radeon_card_posted(rdev) && rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
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
	r520_mc_init(rdev);
	rv515_debugfs(rdev);
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
	r = rv370_pcie_gart_init(rdev);
	if (r)
		return r;
	rv515_set_safe_registers(rdev);
	rdev->accel_working = true;
	r = r520_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		r100_wb_fini(rdev);
		r100_ib_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rv370_pcie_gart_fini(rdev);
		radeon_agp_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
