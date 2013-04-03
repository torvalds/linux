/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "drmP.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "cikd.h"
#include "atom.h"
#include "cik_blit_shaders.h"

/* GFX */
#define CIK_PFP_UCODE_SIZE 2144
#define CIK_ME_UCODE_SIZE 2144
#define CIK_CE_UCODE_SIZE 2144
/* compute */
#define CIK_MEC_UCODE_SIZE 4192
/* interrupts */
#define BONAIRE_RLC_UCODE_SIZE 2048
#define KB_RLC_UCODE_SIZE 2560
#define KV_RLC_UCODE_SIZE 2560
/* gddr controller */
#define CIK_MC_UCODE_SIZE 7866
/* sdma */
#define CIK_SDMA_UCODE_SIZE 1050
#define CIK_SDMA_UCODE_VERSION 64

MODULE_FIRMWARE("radeon/BONAIRE_pfp.bin");
MODULE_FIRMWARE("radeon/BONAIRE_me.bin");
MODULE_FIRMWARE("radeon/BONAIRE_ce.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mec.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mc.bin");
MODULE_FIRMWARE("radeon/BONAIRE_rlc.bin");
MODULE_FIRMWARE("radeon/BONAIRE_sdma.bin");
MODULE_FIRMWARE("radeon/KAVERI_pfp.bin");
MODULE_FIRMWARE("radeon/KAVERI_me.bin");
MODULE_FIRMWARE("radeon/KAVERI_ce.bin");
MODULE_FIRMWARE("radeon/KAVERI_mec.bin");
MODULE_FIRMWARE("radeon/KAVERI_rlc.bin");
MODULE_FIRMWARE("radeon/KAVERI_sdma.bin");
MODULE_FIRMWARE("radeon/KABINI_pfp.bin");
MODULE_FIRMWARE("radeon/KABINI_me.bin");
MODULE_FIRMWARE("radeon/KABINI_ce.bin");
MODULE_FIRMWARE("radeon/KABINI_mec.bin");
MODULE_FIRMWARE("radeon/KABINI_rlc.bin");
MODULE_FIRMWARE("radeon/KABINI_sdma.bin");

extern int r600_ih_ring_alloc(struct radeon_device *rdev);
extern void r600_ih_ring_fini(struct radeon_device *rdev);
extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern bool evergreen_is_display_hung(struct radeon_device *rdev);
extern void si_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
extern void si_rlc_fini(struct radeon_device *rdev);
extern int si_rlc_init(struct radeon_device *rdev);
static void cik_rlc_stop(struct radeon_device *rdev);

/*
 * Indirect registers accessor
 */
u32 cik_pciep_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(PCIE_INDEX, reg);
	(void)RREG32(PCIE_INDEX);
	r = RREG32(PCIE_DATA);
	return r;
}

void cik_pciep_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(PCIE_INDEX, reg);
	(void)RREG32(PCIE_INDEX);
	WREG32(PCIE_DATA, v);
	(void)RREG32(PCIE_DATA);
}

/**
 * cik_get_xclk - get the xclk
 *
 * @rdev: radeon_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (CIK).
 */
u32 cik_get_xclk(struct radeon_device *rdev)
{
        u32 reference_clock = rdev->clock.spll.reference_freq;

	if (rdev->flags & RADEON_IS_IGP) {
		if (RREG32_SMC(GENERAL_PWRMGT) & GPU_COUNTER_CLK)
			return reference_clock / 2;
	} else {
		if (RREG32_SMC(CG_CLKPIN_CNTL) & XTALIN_DIVIDE)
			return reference_clock / 4;
	}
	return reference_clock;
}

#define BONAIRE_IO_MC_REGS_SIZE 36

static const u32 bonaire_io_mc_regs[BONAIRE_IO_MC_REGS_SIZE][2] =
{
	{0x00000070, 0x04400000},
	{0x00000071, 0x80c01803},
	{0x00000072, 0x00004004},
	{0x00000073, 0x00000100},
	{0x00000074, 0x00ff0000},
	{0x00000075, 0x34000000},
	{0x00000076, 0x08000014},
	{0x00000077, 0x00cc08ec},
	{0x00000078, 0x00000400},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x04090000},
	{0x0000007c, 0x00000000},
	{0x0000007e, 0x4408a8e8},
	{0x0000007f, 0x00000304},
	{0x00000080, 0x00000000},
	{0x00000082, 0x00000001},
	{0x00000083, 0x00000002},
	{0x00000084, 0xf3e4f400},
	{0x00000085, 0x052024e3},
	{0x00000087, 0x00000000},
	{0x00000088, 0x01000000},
	{0x0000008a, 0x1c0a0000},
	{0x0000008b, 0xff010000},
	{0x0000008d, 0xffffefff},
	{0x0000008e, 0xfff3efff},
	{0x0000008f, 0xfff3efbf},
	{0x00000092, 0xf7ffffff},
	{0x00000093, 0xffffff7f},
	{0x00000095, 0x00101101},
	{0x00000096, 0x00000fff},
	{0x00000097, 0x00116fff},
	{0x00000098, 0x60010000},
	{0x00000099, 0x10010000},
	{0x0000009a, 0x00006000},
	{0x0000009b, 0x00001000},
	{0x0000009f, 0x00b48000}
};

/* ucode loading */
/**
 * ci_mc_load_microcode - load MC ucode into the hw
 *
 * @rdev: radeon_device pointer
 *
 * Load the GDDR MC ucode into the hw (CIK).
 * Returns 0 on success, error on failure.
 */
static int ci_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	u32 running, blackout = 0;
	u32 *io_mc_regs;
	int i, ucode_size, regs_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	switch (rdev->family) {
	case CHIP_BONAIRE:
	default:
		io_mc_regs = (u32 *)&bonaire_io_mc_regs;
		ucode_size = CIK_MC_UCODE_SIZE;
		regs_size = BONAIRE_IO_MC_REGS_SIZE;
		break;
	}

	running = RREG32(MC_SEQ_SUP_CNTL) & RUN_MASK;

	if (running == 0) {
		if (running) {
			blackout = RREG32(MC_SHARED_BLACKOUT_CNTL);
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout | 1);
		}

		/* reset the engine and set to writable */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000010);

		/* load mc io regs */
		for (i = 0; i < regs_size; i++) {
			WREG32(MC_SEQ_IO_DEBUG_INDEX, io_mc_regs[(i << 1)]);
			WREG32(MC_SEQ_IO_DEBUG_DATA, io_mc_regs[(i << 1) + 1]);
		}
		/* load the MC ucode */
		fw_data = (const __be32 *)rdev->mc_fw->data;
		for (i = 0; i < ucode_size; i++)
			WREG32(MC_SEQ_SUP_PGM, be32_to_cpup(fw_data++));

		/* put the engine back into the active state */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000004);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000001);

		/* wait for training to complete */
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D0)
				break;
			udelay(1);
		}
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D1)
				break;
			udelay(1);
		}

		if (running)
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout);
	}

	return 0;
}

/**
 * cik_init_microcode - load ucode images from disk
 *
 * @rdev: radeon_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int cik_init_microcode(struct radeon_device *rdev)
{
	struct platform_device *pdev;
	const char *chip_name;
	size_t pfp_req_size, me_req_size, ce_req_size,
		mec_req_size, rlc_req_size, mc_req_size,
		sdma_req_size;
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
	case CHIP_BONAIRE:
		chip_name = "BONAIRE";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = BONAIRE_RLC_UCODE_SIZE * 4;
		mc_req_size = CIK_MC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		break;
	case CHIP_KAVERI:
		chip_name = "KAVERI";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KV_RLC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		break;
	case CHIP_KABINI:
		chip_name = "KABINI";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KB_RLC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		break;
	default: BUG();
	}

	DRM_INFO("Loading %s Microcode\n", chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->pfp_fw->size != pfp_req_size) {
		printk(KERN_ERR
		       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
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
		       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", chip_name);
	err = request_firmware(&rdev->ce_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->ce_fw->size != ce_req_size) {
		printk(KERN_ERR
		       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->ce_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_mec.bin", chip_name);
	err = request_firmware(&rdev->mec_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->mec_fw->size != mec_req_size) {
		printk(KERN_ERR
		       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->mec_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->rlc_fw->size != rlc_req_size) {
		printk(KERN_ERR
		       "cik_rlc: Bogus length %zu in firmware \"%s\"\n",
		       rdev->rlc_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_sdma.bin", chip_name);
	err = request_firmware(&rdev->sdma_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->sdma_fw->size != sdma_req_size) {
		printk(KERN_ERR
		       "cik_sdma: Bogus length %zu in firmware \"%s\"\n",
		       rdev->sdma_fw->size, fw_name);
		err = -EINVAL;
	}

	/* No MC ucode on APUs */
	if (!(rdev->flags & RADEON_IS_IGP)) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
		err = request_firmware(&rdev->mc_fw, fw_name, &pdev->dev);
		if (err)
			goto out;
		if (rdev->mc_fw->size != mc_req_size) {
			printk(KERN_ERR
			       "cik_mc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mc_fw->size, fw_name);
			err = -EINVAL;
		}
	}

out:
	platform_device_unregister(pdev);

	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "cik_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
		release_firmware(rdev->ce_fw);
		rdev->ce_fw = NULL;
		release_firmware(rdev->rlc_fw);
		rdev->rlc_fw = NULL;
		release_firmware(rdev->mc_fw);
		rdev->mc_fw = NULL;
	}
	return err;
}

/*
 * Core functions
 */
/**
 * cik_tiling_mode_table_init - init the hw tiling table
 *
 * @rdev: radeon_device pointer
 *
 * Starting with SI, the tiling setup is done globally in a
 * set of 32 tiling modes.  Rather than selecting each set of
 * parameters per surface as on older asics, we just select
 * which index in the tiling table we want to use, and the
 * surface uses those parameters (CIK).
 */
static void cik_tiling_mode_table_init(struct radeon_device *rdev)
{
	const u32 num_tile_mode_states = 32;
	const u32 num_secondary_tile_mode_states = 16;
	u32 reg_offset, gb_tile_moden, split_equal_to_row_size;
	u32 num_pipe_configs;
	u32 num_rbs = rdev->config.cik.max_backends_per_se *
		rdev->config.cik.max_shader_engines;

	switch (rdev->config.cik.mem_row_size_in_kb) {
	case 1:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_1KB;
		break;
	case 2:
	default:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_2KB;
		break;
	case 4:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_4KB;
		break;
	}

	num_pipe_configs = rdev->config.cik.max_tile_pipes;
	if (num_pipe_configs > 8)
		num_pipe_configs = 8; /* ??? */

	if (num_pipe_configs == 8) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 4) {
		if (num_rbs == 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		} else if (num_rbs < 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 2) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = ARRAY_MODE(ARRAY_LINEAR_ALIGNED);
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else
		DRM_ERROR("unknown num pipe config: 0x%x\n", num_pipe_configs);
}

/**
 * cik_select_se_sh - select which SE, SH to address
 *
 * @rdev: radeon_device pointer
 * @se_num: shader engine to address
 * @sh_num: sh block to address
 *
 * Select which SE, SH combinations to address. Certain
 * registers are instanced per SE or SH.  0xffffffff means
 * broadcast to all SEs or SHs (CIK).
 */
static void cik_select_se_sh(struct radeon_device *rdev,
			     u32 se_num, u32 sh_num)
{
	u32 data = INSTANCE_BROADCAST_WRITES;

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff))
		data = SH_BROADCAST_WRITES | SE_BROADCAST_WRITES;
	else if (se_num == 0xffffffff)
		data |= SE_BROADCAST_WRITES | SH_INDEX(sh_num);
	else if (sh_num == 0xffffffff)
		data |= SH_BROADCAST_WRITES | SE_INDEX(se_num);
	else
		data |= SH_INDEX(sh_num) | SE_INDEX(se_num);
	WREG32(GRBM_GFX_INDEX, data);
}

/**
 * cik_create_bitmask - create a bitmask
 *
 * @bit_width: length of the mask
 *
 * create a variable length bit mask (CIK).
 * Returns the bitmask.
 */
static u32 cik_create_bitmask(u32 bit_width)
{
	u32 i, mask = 0;

	for (i = 0; i < bit_width; i++) {
		mask <<= 1;
		mask |= 1;
	}
	return mask;
}

/**
 * cik_select_se_sh - select which SE, SH to address
 *
 * @rdev: radeon_device pointer
 * @max_rb_num: max RBs (render backends) for the asic
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 *
 * Calculates the bitmask of disabled RBs (CIK).
 * Returns the disabled RB bitmask.
 */
static u32 cik_get_rb_disabled(struct radeon_device *rdev,
			      u32 max_rb_num, u32 se_num,
			      u32 sh_per_se)
{
	u32 data, mask;

	data = RREG32(CC_RB_BACKEND_DISABLE);
	if (data & 1)
		data &= BACKEND_DISABLE_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_RB_BACKEND_DISABLE);

	data >>= BACKEND_DISABLE_SHIFT;

	mask = cik_create_bitmask(max_rb_num / se_num / sh_per_se);

	return data & mask;
}

/**
 * cik_setup_rb - setup the RBs on the asic
 *
 * @rdev: radeon_device pointer
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 * @max_rb_num: max RBs (render backends) for the asic
 *
 * Configures per-SE/SH RB registers (CIK).
 */
static void cik_setup_rb(struct radeon_device *rdev,
			 u32 se_num, u32 sh_per_se,
			 u32 max_rb_num)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			cik_select_se_sh(rdev, i, j);
			data = cik_get_rb_disabled(rdev, max_rb_num, se_num, sh_per_se);
			disabled_rbs |= data << ((i * sh_per_se + j) * CIK_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	mask = 1;
	for (i = 0; i < max_rb_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	for (i = 0; i < se_num; i++) {
		cik_select_se_sh(rdev, i, 0xffffffff);
		data = 0;
		for (j = 0; j < sh_per_se; j++) {
			switch (enabled_rbs & 3) {
			case 1:
				data |= (RASTER_CONFIG_RB_MAP_0 << (i * sh_per_se + j) * 2);
				break;
			case 2:
				data |= (RASTER_CONFIG_RB_MAP_3 << (i * sh_per_se + j) * 2);
				break;
			case 3:
			default:
				data |= (RASTER_CONFIG_RB_MAP_2 << (i * sh_per_se + j) * 2);
				break;
			}
			enabled_rbs >>= 2;
		}
		WREG32(PA_SC_RASTER_CONFIG, data);
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

/**
 * cik_gpu_init - setup the 3D engine
 *
 * @rdev: radeon_device pointer
 *
 * Configures the 3D engine and tiling configuration
 * registers so that the 3D engine is usable.
 */
static void cik_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = RREG32(GB_ADDR_CONFIG);
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 hdp_host_path_cntl;
	u32 tmp;
	int i, j;

	switch (rdev->family) {
	case CHIP_BONAIRE:
		rdev->config.cik.max_shader_engines = 2;
		rdev->config.cik.max_tile_pipes = 4;
		rdev->config.cik.max_cu_per_sh = 7;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 2;
		rdev->config.cik.max_texture_channel_caches = 4;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 32;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_KAVERI:
		/* TODO */
		break;
	case CHIP_KABINI:
	default:
		rdev->config.cik.max_shader_engines = 1;
		rdev->config.cik.max_tile_pipes = 2;
		rdev->config.cik.max_cu_per_sh = 2;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 1;
		rdev->config.cik.max_texture_channel_caches = 2;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 16;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
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

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	rdev->config.cik.num_tile_pipes = rdev->config.cik.max_tile_pipes;
	rdev->config.cik.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.cik.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.cik.mem_row_size_in_kb > 4)
		rdev->config.cik.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.cik.shader_engine_tile_size = 32;
	rdev->config.cik.num_gpus = 1;
	rdev->config.cik.multi_gpu_tile_size = 64;

	/* fix up row size */
	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (rdev->config.cik.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config |= ROW_SIZE(0);
		break;
	case 2:
		gb_addr_config |= ROW_SIZE(1);
		break;
	case 4:
		gb_addr_config |= ROW_SIZE(2);
		break;
	}

	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.cik.tile_config = 0;
	switch (rdev->config.cik.num_tile_pipes) {
	case 1:
		rdev->config.cik.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.cik.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.cik.tile_config |= (2 << 0);
		break;
	case 8:
	default:
		/* XXX what about 12? */
		rdev->config.cik.tile_config |= (3 << 0);
		break;
	}
	if ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT)
		rdev->config.cik.tile_config |= 1 << 4;
	else
		rdev->config.cik.tile_config |= 0 << 4;
	rdev->config.cik.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.cik.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(SDMA0_TILING_CONFIG + SDMA0_REGISTER_OFFSET, gb_addr_config & 0x70);
	WREG32(SDMA0_TILING_CONFIG + SDMA1_REGISTER_OFFSET, gb_addr_config & 0x70);

	cik_tiling_mode_table_init(rdev);

	cik_setup_rb(rdev, rdev->config.cik.max_shader_engines,
		     rdev->config.cik.max_sh_per_se,
		     rdev->config.cik.max_backends_per_se);

	/* set HW defaults for 3D engine */
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	WREG32(SX_DEBUG_1, 0x20);

	WREG32(TA_CNTL_AUX, 0x00010000);

	tmp = RREG32(SPI_CONFIG_CNTL);
	tmp |= 0x03000000;
	WREG32(SPI_CONFIG_CNTL, tmp);

	WREG32(SQ_CONFIG, 1);

	WREG32(DB_DEBUG, 0);

	tmp = RREG32(DB_DEBUG2) & ~0xf00fffff;
	tmp |= 0x00000400;
	WREG32(DB_DEBUG2, tmp);

	tmp = RREG32(DB_DEBUG3) & ~0x0002021c;
	tmp |= 0x00020200;
	WREG32(DB_DEBUG3, tmp);

	tmp = RREG32(CB_HW_CONTROL) & ~0x00010000;
	tmp |= 0x00018208;
	WREG32(CB_HW_CONTROL, tmp);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.cik.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.cik.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_CONFIG, 0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));
	WREG32(PA_SC_ENHANCE, ENABLE_PA_SC_OUT_OF_ORDER);

	udelay(50);
}

/*
 * GPU scratch registers helpers function.
 */
/**
 * cik_scratch_init - setup driver info for CP scratch regs
 *
 * @rdev: radeon_device pointer
 *
 * Set up the number and offset of the CP scratch registers.
 * NOTE: use of CP scratch registers is a legacy inferface and
 * is not used by default on newer asics (r6xx+).  On newer asics,
 * memory buffers are used for fences rather than scratch regs.
 */
static void cik_scratch_init(struct radeon_device *rdev)
{
	int i;

	rdev->scratch.num_reg = 7;
	rdev->scratch.reg_base = SCRATCH_REG0;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = rdev->scratch.reg_base + (i * 4);
	}
}

/**
 * cik_ring_test - basic gfx ring test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Allocate a scratch register and write to it using the gfx ring (CIK).
 * Provides a basic gfx ring test to verify that the ring is working.
 * Used by cik_cp_gfx_resume();
 * Returns 0 on success, error on failure.
 */
int cik_ring_test(struct radeon_device *rdev, struct radeon_ring *ring)
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
	r = radeon_ring_lock(rdev, ring, 3);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring %d (%d).\n", ring->idx, r);
		radeon_scratch_free(rdev, scratch);
		return r;
	}
	radeon_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	radeon_ring_write(ring, ((scratch - PACKET3_SET_UCONFIG_REG_START) >> 2));
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (scratch(0x%04X)=0x%08X)\n",
			  ring->idx, scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	return r;
}

/**
 * cik_fence_ring_emit - emit a fence on the gfx ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Emits a fence sequnce number on the gfx ring and flushes
 * GPU caches.
 */
void cik_fence_ring_emit(struct radeon_device *rdev,
			 struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* EVENT_WRITE_EOP - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xffff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
	/* HDP flush */
	/* We should be using the new WAIT_REG_MEM special op packet here
	 * but it causes the CP to hang
	 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, HDP_MEM_COHERENCY_FLUSH_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);
}

void cik_semaphore_ring_emit(struct radeon_device *rdev,
			     struct radeon_ring *ring,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait)
{
	uint64_t addr = semaphore->gpu_addr;
	unsigned sel = emit_wait ? PACKET3_SEM_SEL_WAIT : PACKET3_SEM_SEL_SIGNAL;

	radeon_ring_write(ring, PACKET3(PACKET3_MEM_SEMAPHORE, 1));
	radeon_ring_write(ring, addr & 0xffffffff);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xffff) | sel);
}

/*
 * IB stuff
 */
/**
 * cik_ring_ib_execute - emit an IB (Indirect Buffer) on the gfx ring
 *
 * @rdev: radeon_device pointer
 * @ib: radeon indirect buffer object
 *
 * Emits an DE (drawing engine) or CE (constant engine) IB
 * on the gfx ring.  IBs are usually generated by userspace
 * acceleration drivers and submitted to the kernel for
 * sheduling on the ring.  This function schedules the IB
 * on the gfx ring for execution by the GPU.
 */
void cik_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	u32 header, control = INDIRECT_BUFFER_VALID;

	if (ib->is_const_ib) {
		/* set switch buffer packet before const IB */
		radeon_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		radeon_ring_write(ring, 0);

		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	} else {
		u32 next_rptr;
		if (ring->rptr_save_reg) {
			next_rptr = ring->wptr + 3 + 4;
			radeon_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
			radeon_ring_write(ring, ((ring->rptr_save_reg -
						  PACKET3_SET_UCONFIG_REG_START) >> 2));
			radeon_ring_write(ring, next_rptr);
		} else if (rdev->wb.enabled) {
			next_rptr = ring->wptr + 5 + 4;
			radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
			radeon_ring_write(ring, WRITE_DATA_DST_SEL(1));
			radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
			radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
			radeon_ring_write(ring, next_rptr);
		}

		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);
	}

	control |= ib->length_dw |
		(ib->vm ? (ib->vm->id << 24) : 0);

	radeon_ring_write(ring, header);
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	radeon_ring_write(ring, control);
}

/**
 * cik_ib_test - basic gfx ring IB test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Allocate an IB and execute it on the gfx ring (CIK).
 * Provides a basic gfx ring test to verify that IBs are working.
 * Returns 0 on success, error on failure.
 */
int cik_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
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
	r = radeon_ib_get(rdev, ring->idx, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}
	ib.ptr[0] = PACKET3(PACKET3_SET_UCONFIG_REG, 1);
	ib.ptr[1] = ((scratch - PACKET3_SET_UCONFIG_REG_START) >> 2);
	ib.ptr[2] = 0xDEADBEEF;
	ib.length_dw = 3;
	r = radeon_ib_schedule(rdev, &ib, NULL);
	if (r) {
		radeon_scratch_free(rdev, scratch);
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait(ib.fence, false);
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
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n", ib.fence->ring, i);
	} else {
		DRM_ERROR("radeon: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	radeon_ib_free(rdev, &ib);
	return r;
}

/*
 * CP.
 * On CIK, gfx and compute now have independant command processors.
 *
 * GFX
 * Gfx consists of a single ring and can process both gfx jobs and
 * compute jobs.  The gfx CP consists of three microengines (ME):
 * PFP - Pre-Fetch Parser
 * ME - Micro Engine
 * CE - Constant Engine
 * The PFP and ME make up what is considered the Drawing Engine (DE).
 * The CE is an asynchronous engine used for updating buffer desciptors
 * used by the DE so that they can be loaded into cache in parallel
 * while the DE is processing state update packets.
 *
 * Compute
 * The compute CP consists of two microengines (ME):
 * MEC1 - Compute MicroEngine 1
 * MEC2 - Compute MicroEngine 2
 * Each MEC supports 4 compute pipes and each pipe supports 8 queues.
 * The queues are exposed to userspace and are programmed directly
 * by the compute runtime.
 */
/**
 * cik_cp_gfx_enable - enable/disable the gfx CP MEs
 *
 * @rdev: radeon_device pointer
 * @enable: enable or disable the MEs
 *
 * Halts or unhalts the gfx MEs.
 */
static void cik_cp_gfx_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT));
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	}
	udelay(50);
}

/**
 * cik_cp_gfx_load_microcode - load the gfx CP ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the gfx PFP, ME, and CE ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_cp_gfx_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw)
		return -EINVAL;

	cik_cp_gfx_enable(rdev, false);

	/* PFP */
	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < CIK_PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	/* CE */
	fw_data = (const __be32 *)rdev->ce_fw->data;
	WREG32(CP_CE_UCODE_ADDR, 0);
	for (i = 0; i < CIK_CE_UCODE_SIZE; i++)
		WREG32(CP_CE_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_CE_UCODE_ADDR, 0);

	/* ME */
	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < CIK_ME_UCODE_SIZE; i++)
		WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_ME_RAM_WADDR, 0);

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_CE_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

/**
 * cik_cp_gfx_start - start the gfx ring
 *
 * @rdev: radeon_device pointer
 *
 * Enables the ring and loads the clear state context and other
 * packets required to init the ring.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_gfx_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	/* init the CP */
	WREG32(CP_MAX_CONTEXT, rdev->config.cik.max_hw_contexts - 1);
	WREG32(CP_ENDIAN_SWAP, 0);
	WREG32(CP_DEVICE_ID, 1);

	cik_cp_gfx_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, cik_default_size + 17);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* init the CE partitions.  CE only used for gfx on CIK */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	radeon_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	radeon_ring_write(ring, 0xc000);
	radeon_ring_write(ring, 0xc000);

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	radeon_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	radeon_ring_write(ring, 0x80000000);
	radeon_ring_write(ring, 0x80000000);

	for (i = 0; i < cik_default_size; i++)
		radeon_ring_write(ring, cik_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /* VGT_OUT_DEALLOC_CNTL */

	radeon_ring_unlock_commit(rdev, ring);

	return 0;
}

/**
 * cik_cp_gfx_fini - stop the gfx ring
 *
 * @rdev: radeon_device pointer
 *
 * Stop the gfx ring and tear down the driver ring
 * info.
 */
static void cik_cp_gfx_fini(struct radeon_device *rdev)
{
	cik_cp_gfx_enable(rdev, false);
	radeon_ring_fini(rdev, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
}

/**
 * cik_cp_gfx_resume - setup the gfx ring buffer registers
 *
 * @rdev: radeon_device pointer
 *
 * Program the location and size of the gfx ring buffer
 * and test it to make sure it's working.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_gfx_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr;
	int r;

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32(CP_RB_VMID, 0);

	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);

	/* ring 0 - compute and gfx */
	/* Set ring buffer size */
	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	rb_bufsz = drm_order(ring->ring_size / 8);
	tmp = (drm_order(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB0_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB0_WPTR, ring->wptr);

	/* set the wb address wether it's enabled or not */
	WREG32(CP_RB0_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB0_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFF);

	/* scratch register shadowing is no longer supported */
	WREG32(SCRATCH_UMSK, 0);

	if (!rdev->wb.enabled)
		tmp |= RB_NO_UPDATE;

	mdelay(1);
	WREG32(CP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32(CP_RB0_BASE, rb_addr);
	WREG32(CP_RB0_BASE_HI, upper_32_bits(rb_addr));

	ring->rptr = RREG32(CP_RB0_RPTR);

	/* start the ring */
	cik_cp_gfx_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		return r;
	}
	return 0;
}

/**
 * cik_cp_compute_enable - enable/disable the compute CP MEs
 *
 * @rdev: radeon_device pointer
 * @enable: enable or disable the MEs
 *
 * Halts or unhalts the compute MEs.
 */
static void cik_cp_compute_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_MEC_CNTL, 0);
	else
		WREG32(CP_MEC_CNTL, (MEC_ME1_HALT | MEC_ME2_HALT));
	udelay(50);
}

/**
 * cik_cp_compute_load_microcode - load the compute CP ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the compute MEC1&2 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_cp_compute_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->mec_fw)
		return -EINVAL;

	cik_cp_compute_enable(rdev, false);

	/* MEC1 */
	fw_data = (const __be32 *)rdev->mec_fw->data;
	WREG32(CP_MEC_ME1_UCODE_ADDR, 0);
	for (i = 0; i < CIK_MEC_UCODE_SIZE; i++)
		WREG32(CP_MEC_ME1_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_MEC_ME1_UCODE_ADDR, 0);

	if (rdev->family == CHIP_KAVERI) {
		/* MEC2 */
		fw_data = (const __be32 *)rdev->mec_fw->data;
		WREG32(CP_MEC_ME2_UCODE_ADDR, 0);
		for (i = 0; i < CIK_MEC_UCODE_SIZE; i++)
			WREG32(CP_MEC_ME2_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_MEC_ME2_UCODE_ADDR, 0);
	}

	return 0;
}

/**
 * cik_cp_compute_start - start the compute queues
 *
 * @rdev: radeon_device pointer
 *
 * Enable the compute queues.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_compute_start(struct radeon_device *rdev)
{
	//todo
	return 0;
}

/**
 * cik_cp_compute_fini - stop the compute queues
 *
 * @rdev: radeon_device pointer
 *
 * Stop the compute queues and tear down the driver queue
 * info.
 */
static void cik_cp_compute_fini(struct radeon_device *rdev)
{
	cik_cp_compute_enable(rdev, false);
	//todo
}

/**
 * cik_cp_compute_resume - setup the compute queue registers
 *
 * @rdev: radeon_device pointer
 *
 * Program the compute queues and test them to make sure they
 * are working.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_compute_resume(struct radeon_device *rdev)
{
	int r;

	//todo
	r = cik_cp_compute_start(rdev);
	if (r)
		return r;
	return 0;
}

/* XXX temporary wrappers to handle both compute and gfx */
/* XXX */
static void cik_cp_enable(struct radeon_device *rdev, bool enable)
{
	cik_cp_gfx_enable(rdev, enable);
	cik_cp_compute_enable(rdev, enable);
}

/* XXX */
static int cik_cp_load_microcode(struct radeon_device *rdev)
{
	int r;

	r = cik_cp_gfx_load_microcode(rdev);
	if (r)
		return r;
	r = cik_cp_compute_load_microcode(rdev);
	if (r)
		return r;

	return 0;
}

/* XXX */
static void cik_cp_fini(struct radeon_device *rdev)
{
	cik_cp_gfx_fini(rdev);
	cik_cp_compute_fini(rdev);
}

/* XXX */
static int cik_cp_resume(struct radeon_device *rdev)
{
	int r;

	/* Reset all cp blocks */
	WREG32(GRBM_SOFT_RESET, SOFT_RESET_CP);
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);
	RREG32(GRBM_SOFT_RESET);

	r = cik_cp_load_microcode(rdev);
	if (r)
		return r;

	r = cik_cp_gfx_resume(rdev);
	if (r)
		return r;
	r = cik_cp_compute_resume(rdev);
	if (r)
		return r;

	return 0;
}

/*
 * sDMA - System DMA
 * Starting with CIK, the GPU has new asynchronous
 * DMA engines.  These engines are used for compute
 * and gfx.  There are two DMA engines (SDMA0, SDMA1)
 * and each one supports 1 ring buffer used for gfx
 * and 2 queues used for compute.
 *
 * The programming model is very similar to the CP
 * (ring buffer, IBs, etc.), but sDMA has it's own
 * packet format that is different from the PM4 format
 * used by the CP. sDMA supports copying data, writing
 * embedded data, solid fills, and a number of other
 * things.  It also has support for tiling/detiling of
 * buffers.
 */
/**
 * cik_sdma_ring_ib_execute - Schedule an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (CIK).
 */
void cik_sdma_ring_ib_execute(struct radeon_device *rdev,
			      struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	u32 extra_bits = (ib->vm ? ib->vm->id : 0) & 0xf;

	if (rdev->wb.enabled) {
		u32 next_rptr = ring->wptr + 5;
		while ((next_rptr & 7) != 4)
			next_rptr++;
		next_rptr += 4;
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
		radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
		radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
		radeon_ring_write(ring, 1); /* number of DWs to follow */
		radeon_ring_write(ring, next_rptr);
	}

	/* IB packet must end on a 8 DW boundary */
	while ((ring->wptr & 7) != 4)
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_INDIRECT_BUFFER, 0, extra_bits));
	radeon_ring_write(ring, ib->gpu_addr & 0xffffffe0); /* base must be 32 byte aligned */
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xffffffff);
	radeon_ring_write(ring, ib->length_dw);

}

/**
 * cik_sdma_fence_ring_emit - emit a fence on the DMA ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (CIK).
 */
void cik_sdma_fence_ring_emit(struct radeon_device *rdev,
			      struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(1) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(3)); /* == */
	u32 ref_and_mask;

	if (fence->ring == R600_RING_TYPE_DMA_INDEX)
		ref_and_mask = SDMA0;
	else
		ref_and_mask = SDMA1;

	/* write the fence */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_FENCE, 0, 0));
	radeon_ring_write(ring, addr & 0xffffffff);
	radeon_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	radeon_ring_write(ring, fence->seq);
	/* generate an interrupt */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_TRAP, 0, 0));
	/* flush HDP */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	radeon_ring_write(ring, GPU_HDP_FLUSH_DONE);
	radeon_ring_write(ring, GPU_HDP_FLUSH_REQ);
	radeon_ring_write(ring, ref_and_mask); /* REFERENCE */
	radeon_ring_write(ring, ref_and_mask); /* MASK */
	radeon_ring_write(ring, (4 << 16) | 10); /* RETRY_COUNT, POLL_INTERVAL */
}

/**
 * cik_sdma_semaphore_ring_emit - emit a semaphore on the dma ring
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @semaphore: radeon semaphore object
 * @emit_wait: wait or signal semaphore
 *
 * Add a DMA semaphore packet to the ring wait on or signal
 * other rings (CIK).
 */
void cik_sdma_semaphore_ring_emit(struct radeon_device *rdev,
				  struct radeon_ring *ring,
				  struct radeon_semaphore *semaphore,
				  bool emit_wait)
{
	u64 addr = semaphore->gpu_addr;
	u32 extra_bits = emit_wait ? 0 : SDMA_SEMAPHORE_EXTRA_S;

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SEMAPHORE, 0, extra_bits));
	radeon_ring_write(ring, addr & 0xfffffff8);
	radeon_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
}

/**
 * cik_sdma_gfx_stop - stop the gfx async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the gfx async dma ring buffers (CIK).
 */
static void cik_sdma_gfx_stop(struct radeon_device *rdev)
{
	u32 rb_cntl, reg_offset;
	int i;

	radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);

	for (i = 0; i < 2; i++) {
		if (i == 0)
			reg_offset = SDMA0_REGISTER_OFFSET;
		else
			reg_offset = SDMA1_REGISTER_OFFSET;
		rb_cntl = RREG32(SDMA0_GFX_RB_CNTL + reg_offset);
		rb_cntl &= ~SDMA_RB_ENABLE;
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl);
		WREG32(SDMA0_GFX_IB_CNTL + reg_offset, 0);
	}
}

/**
 * cik_sdma_rlc_stop - stop the compute async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the compute async dma queues (CIK).
 */
static void cik_sdma_rlc_stop(struct radeon_device *rdev)
{
	/* XXX todo */
}

/**
 * cik_sdma_enable - stop the async dma engines
 *
 * @rdev: radeon_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (CIK).
 */
static void cik_sdma_enable(struct radeon_device *rdev, bool enable)
{
	u32 me_cntl, reg_offset;
	int i;

	for (i = 0; i < 2; i++) {
		if (i == 0)
			reg_offset = SDMA0_REGISTER_OFFSET;
		else
			reg_offset = SDMA1_REGISTER_OFFSET;
		me_cntl = RREG32(SDMA0_ME_CNTL + reg_offset);
		if (enable)
			me_cntl &= ~SDMA_HALT;
		else
			me_cntl |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + reg_offset, me_cntl);
	}
}

/**
 * cik_sdma_gfx_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_gfx_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 reg_offset, wb_offset;
	int i, r;

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
			reg_offset = SDMA0_REGISTER_OFFSET;
			wb_offset = R600_WB_DMA_RPTR_OFFSET;
		} else {
			ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
			reg_offset = SDMA1_REGISTER_OFFSET;
			wb_offset = CAYMAN_WB_DMA1_RPTR_OFFSET;
		}

		WREG32(SDMA0_SEM_INCOMPLETE_TIMER_CNTL + reg_offset, 0);
		WREG32(SDMA0_SEM_WAIT_FAIL_TIMER_CNTL + reg_offset, 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = drm_order(ring->ring_size / 4);
		rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
		rb_cntl |= SDMA_RB_SWAP_ENABLE | SDMA_RPTR_WRITEBACK_SWAP_ENABLE;
#endif
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(SDMA0_GFX_RB_RPTR + reg_offset, 0);
		WREG32(SDMA0_GFX_RB_WPTR + reg_offset, 0);

		/* set the wb address whether it's enabled or not */
		WREG32(SDMA0_GFX_RB_RPTR_ADDR_HI + reg_offset,
		       upper_32_bits(rdev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(SDMA0_GFX_RB_RPTR_ADDR_LO + reg_offset,
		       ((rdev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC));

		if (rdev->wb.enabled)
			rb_cntl |= SDMA_RPTR_WRITEBACK_ENABLE;

		WREG32(SDMA0_GFX_RB_BASE + reg_offset, ring->gpu_addr >> 8);
		WREG32(SDMA0_GFX_RB_BASE_HI + reg_offset, ring->gpu_addr >> 40);

		ring->wptr = 0;
		WREG32(SDMA0_GFX_RB_WPTR + reg_offset, ring->wptr << 2);

		ring->rptr = RREG32(SDMA0_GFX_RB_RPTR + reg_offset) >> 2;

		/* enable DMA RB */
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl | SDMA_RB_ENABLE);

		ib_cntl = SDMA_IB_ENABLE;
#ifdef __BIG_ENDIAN
		ib_cntl |= SDMA_IB_SWAP_ENABLE;
#endif
		/* enable DMA IBs */
		WREG32(SDMA0_GFX_IB_CNTL + reg_offset, ib_cntl);

		ring->ready = true;

		r = radeon_ring_test(rdev, ring->idx, ring);
		if (r) {
			ring->ready = false;
			return r;
		}
	}

	radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

/**
 * cik_sdma_rlc_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the compute DMA queues and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_rlc_resume(struct radeon_device *rdev)
{
	/* XXX todo */
	return 0;
}

/**
 * cik_sdma_load_microcode - load the sDMA ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_sdma_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->sdma_fw)
		return -EINVAL;

	/* stop the gfx rings and rlc compute queues */
	cik_sdma_gfx_stop(rdev);
	cik_sdma_rlc_stop(rdev);

	/* halt the MEs */
	cik_sdma_enable(rdev, false);

	/* sdma0 */
	fw_data = (const __be32 *)rdev->sdma_fw->data;
	WREG32(SDMA0_UCODE_ADDR + SDMA0_REGISTER_OFFSET, 0);
	for (i = 0; i < CIK_SDMA_UCODE_SIZE; i++)
		WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, be32_to_cpup(fw_data++));
	WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);

	/* sdma1 */
	fw_data = (const __be32 *)rdev->sdma_fw->data;
	WREG32(SDMA0_UCODE_ADDR + SDMA1_REGISTER_OFFSET, 0);
	for (i = 0; i < CIK_SDMA_UCODE_SIZE; i++)
		WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, be32_to_cpup(fw_data++));
	WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);

	WREG32(SDMA0_UCODE_ADDR + SDMA0_REGISTER_OFFSET, 0);
	WREG32(SDMA0_UCODE_ADDR + SDMA1_REGISTER_OFFSET, 0);
	return 0;
}

/**
 * cik_sdma_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the DMA engines and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_resume(struct radeon_device *rdev)
{
	int r;

	/* Reset dma */
	WREG32(SRBM_SOFT_RESET, SOFT_RESET_SDMA | SOFT_RESET_SDMA1);
	RREG32(SRBM_SOFT_RESET);
	udelay(50);
	WREG32(SRBM_SOFT_RESET, 0);
	RREG32(SRBM_SOFT_RESET);

	r = cik_sdma_load_microcode(rdev);
	if (r)
		return r;

	/* unhalt the MEs */
	cik_sdma_enable(rdev, true);

	/* start the gfx rings and rlc compute queues */
	r = cik_sdma_gfx_resume(rdev);
	if (r)
		return r;
	r = cik_sdma_rlc_resume(rdev);
	if (r)
		return r;

	return 0;
}

/**
 * cik_sdma_fini - tear down the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engines and free the rings (CIK).
 */
static void cik_sdma_fini(struct radeon_device *rdev)
{
	/* stop the gfx rings and rlc compute queues */
	cik_sdma_gfx_stop(rdev);
	cik_sdma_rlc_stop(rdev);
	/* halt the MEs */
	cik_sdma_enable(rdev, false);
	radeon_ring_fini(rdev, &rdev->ring[R600_RING_TYPE_DMA_INDEX]);
	radeon_ring_fini(rdev, &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX]);
	/* XXX - compute dma queue tear down */
}

/**
 * cik_copy_dma - copy pages using the DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @fence: radeon fence object
 *
 * Copy GPU paging using the DMA engine (CIK).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
int cik_copy_dma(struct radeon_device *rdev,
		 uint64_t src_offset, uint64_t dst_offset,
		 unsigned num_gpu_pages,
		 struct radeon_fence **fence)
{
	struct radeon_semaphore *sem = NULL;
	int ring_index = rdev->asic->copy.dma_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_bytes, cur_size_in_bytes;
	int i, num_loops;
	int r = 0;

	r = radeon_semaphore_create(rdev, &sem);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return r;
	}

	size_in_bytes = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT);
	num_loops = DIV_ROUND_UP(size_in_bytes, 0x1fffff);
	r = radeon_ring_lock(rdev, ring, num_loops * 7 + 14);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_semaphore_free(rdev, &sem, NULL);
		return r;
	}

	if (radeon_fence_need_sync(*fence, ring->idx)) {
		radeon_semaphore_sync_rings(rdev, sem, (*fence)->ring,
					    ring->idx);
		radeon_fence_note_sync(*fence, ring->idx);
	} else {
		radeon_semaphore_free(rdev, &sem, NULL);
	}

	for (i = 0; i < num_loops; i++) {
		cur_size_in_bytes = size_in_bytes;
		if (cur_size_in_bytes > 0x1fffff)
			cur_size_in_bytes = 0x1fffff;
		size_in_bytes -= cur_size_in_bytes;
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_LINEAR, 0));
		radeon_ring_write(ring, cur_size_in_bytes);
		radeon_ring_write(ring, 0); /* src/dst endian swap */
		radeon_ring_write(ring, src_offset & 0xffffffff);
		radeon_ring_write(ring, upper_32_bits(src_offset) & 0xffffffff);
		radeon_ring_write(ring, dst_offset & 0xfffffffc);
		radeon_ring_write(ring, upper_32_bits(dst_offset) & 0xffffffff);
		src_offset += cur_size_in_bytes;
		dst_offset += cur_size_in_bytes;
	}

	r = radeon_fence_emit(rdev, fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}

	radeon_ring_unlock_commit(rdev, ring);
	radeon_semaphore_free(rdev, &sem, *fence);

	return r;
}

/**
 * cik_sdma_ring_test - simple async dma engine test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (CIK).
 * Returns 0 for success, error for failure.
 */
int cik_sdma_ring_test(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	unsigned i;
	int r;
	void __iomem *ptr = (void *)rdev->vram_scratch.ptr;
	u32 tmp;

	if (!ptr) {
		DRM_ERROR("invalid vram scratch pointer\n");
		return -EINVAL;
	}

	tmp = 0xCAFEDEAD;
	writel(tmp, ptr);

	r = radeon_ring_lock(rdev, ring, 4);
	if (r) {
		DRM_ERROR("radeon: dma failed to lock ring %d (%d).\n", ring->idx, r);
		return r;
	}
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
	radeon_ring_write(ring, rdev->vram_scratch.gpu_addr & 0xfffffffc);
	radeon_ring_write(ring, upper_32_bits(rdev->vram_scratch.gpu_addr) & 0xffffffff);
	radeon_ring_write(ring, 1); /* number of DWs to follow */
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = readl(ptr);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * cik_sdma_ib_test - test an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (CIK).
 * Returns 0 on success, error on failure.
 */
int cik_sdma_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
	unsigned i;
	int r;
	void __iomem *ptr = (void *)rdev->vram_scratch.ptr;
	u32 tmp = 0;

	if (!ptr) {
		DRM_ERROR("invalid vram scratch pointer\n");
		return -EINVAL;
	}

	tmp = 0xCAFEDEAD;
	writel(tmp, ptr);

	r = radeon_ib_get(rdev, ring->idx, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}

	ib.ptr[0] = SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
	ib.ptr[1] = rdev->vram_scratch.gpu_addr & 0xfffffffc;
	ib.ptr[2] = upper_32_bits(rdev->vram_scratch.gpu_addr) & 0xffffffff;
	ib.ptr[3] = 1;
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = radeon_ib_schedule(rdev, &ib, NULL);
	if (r) {
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait(ib.fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		return r;
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = readl(ptr);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n", ib.fence->ring, i);
	} else {
		DRM_ERROR("radeon: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}
	radeon_ib_free(rdev, &ib);
	return r;
}


static void cik_print_gpu_status_regs(struct radeon_device *rdev)
{
	dev_info(rdev->dev, "  GRBM_STATUS=0x%08X\n",
		RREG32(GRBM_STATUS));
	dev_info(rdev->dev, "  GRBM_STATUS2=0x%08X\n",
		RREG32(GRBM_STATUS2));
	dev_info(rdev->dev, "  GRBM_STATUS_SE0=0x%08X\n",
		RREG32(GRBM_STATUS_SE0));
	dev_info(rdev->dev, "  GRBM_STATUS_SE1=0x%08X\n",
		RREG32(GRBM_STATUS_SE1));
	dev_info(rdev->dev, "  GRBM_STATUS_SE2=0x%08X\n",
		RREG32(GRBM_STATUS_SE2));
	dev_info(rdev->dev, "  GRBM_STATUS_SE3=0x%08X\n",
		RREG32(GRBM_STATUS_SE3));
	dev_info(rdev->dev, "  SRBM_STATUS=0x%08X\n",
		RREG32(SRBM_STATUS));
	dev_info(rdev->dev, "  SRBM_STATUS2=0x%08X\n",
		RREG32(SRBM_STATUS2));
	dev_info(rdev->dev, "  SDMA0_STATUS_REG   = 0x%08X\n",
		RREG32(SDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET));
	dev_info(rdev->dev, "  SDMA1_STATUS_REG   = 0x%08X\n",
		 RREG32(SDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET));
}

/**
 * cik_gpu_check_soft_reset - check which blocks are busy
 *
 * @rdev: radeon_device pointer
 *
 * Check which blocks are busy and return the relevant reset
 * mask to be used by cik_gpu_soft_reset().
 * Returns a mask of the blocks to be reset.
 */
static u32 cik_gpu_check_soft_reset(struct radeon_device *rdev)
{
	u32 reset_mask = 0;
	u32 tmp;

	/* GRBM_STATUS */
	tmp = RREG32(GRBM_STATUS);
	if (tmp & (PA_BUSY | SC_BUSY |
		   BCI_BUSY | SX_BUSY |
		   TA_BUSY | VGT_BUSY |
		   DB_BUSY | CB_BUSY |
		   GDS_BUSY | SPI_BUSY |
		   IA_BUSY | IA_BUSY_NO_DMA))
		reset_mask |= RADEON_RESET_GFX;

	if (tmp & (CP_BUSY | CP_COHERENCY_BUSY))
		reset_mask |= RADEON_RESET_CP;

	/* GRBM_STATUS2 */
	tmp = RREG32(GRBM_STATUS2);
	if (tmp & RLC_BUSY)
		reset_mask |= RADEON_RESET_RLC;

	/* SDMA0_STATUS_REG */
	tmp = RREG32(SDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET);
	if (!(tmp & SDMA_IDLE))
		reset_mask |= RADEON_RESET_DMA;

	/* SDMA1_STATUS_REG */
	tmp = RREG32(SDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET);
	if (!(tmp & SDMA_IDLE))
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS2 */
	tmp = RREG32(SRBM_STATUS2);
	if (tmp & SDMA_BUSY)
		reset_mask |= RADEON_RESET_DMA;

	if (tmp & SDMA1_BUSY)
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS */
	tmp = RREG32(SRBM_STATUS);

	if (tmp & IH_BUSY)
		reset_mask |= RADEON_RESET_IH;

	if (tmp & SEM_BUSY)
		reset_mask |= RADEON_RESET_SEM;

	if (tmp & GRBM_RQ_PENDING)
		reset_mask |= RADEON_RESET_GRBM;

	if (tmp & VMC_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	if (tmp & (MCB_BUSY | MCB_NON_DISPLAY_BUSY |
		   MCC_BUSY | MCD_BUSY))
		reset_mask |= RADEON_RESET_MC;

	if (evergreen_is_display_hung(rdev))
		reset_mask |= RADEON_RESET_DISPLAY;

	/* Skip MC reset as it's mostly likely not hung, just busy */
	if (reset_mask & RADEON_RESET_MC) {
		DRM_DEBUG("MC busy: 0x%08X, clearing.\n", reset_mask);
		reset_mask &= ~RADEON_RESET_MC;
	}

	return reset_mask;
}

/**
 * cik_gpu_soft_reset - soft reset GPU
 *
 * @rdev: radeon_device pointer
 * @reset_mask: mask of which blocks to reset
 *
 * Soft reset the blocks specified in @reset_mask.
 */
static void cik_gpu_soft_reset(struct radeon_device *rdev, u32 reset_mask)
{
	struct evergreen_mc_save save;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if (reset_mask == 0)
		return;

	dev_info(rdev->dev, "GPU softreset: 0x%08X\n", reset_mask);

	cik_print_gpu_status_regs(rdev);
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));

	/* stop the rlc */
	cik_rlc_stop(rdev);

	/* Disable GFX parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	/* Disable MEC parsing/prefetching */
	WREG32(CP_MEC_CNTL, MEC_ME1_HALT | MEC_ME2_HALT);

	if (reset_mask & RADEON_RESET_DMA) {
		/* sdma0 */
		tmp = RREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET);
		tmp |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	}
	if (reset_mask & RADEON_RESET_DMA1) {
		/* sdma1 */
		tmp = RREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET);
		tmp |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	}

	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}

	if (reset_mask & (RADEON_RESET_GFX | RADEON_RESET_COMPUTE | RADEON_RESET_CP))
		grbm_soft_reset = SOFT_RESET_CP | SOFT_RESET_GFX;

	if (reset_mask & RADEON_RESET_CP) {
		grbm_soft_reset |= SOFT_RESET_CP;

		srbm_soft_reset |= SOFT_RESET_GRBM;
	}

	if (reset_mask & RADEON_RESET_DMA)
		srbm_soft_reset |= SOFT_RESET_SDMA;

	if (reset_mask & RADEON_RESET_DMA1)
		srbm_soft_reset |= SOFT_RESET_SDMA1;

	if (reset_mask & RADEON_RESET_DISPLAY)
		srbm_soft_reset |= SOFT_RESET_DC;

	if (reset_mask & RADEON_RESET_RLC)
		grbm_soft_reset |= SOFT_RESET_RLC;

	if (reset_mask & RADEON_RESET_SEM)
		srbm_soft_reset |= SOFT_RESET_SEM;

	if (reset_mask & RADEON_RESET_IH)
		srbm_soft_reset |= SOFT_RESET_IH;

	if (reset_mask & RADEON_RESET_GRBM)
		srbm_soft_reset |= SOFT_RESET_GRBM;

	if (reset_mask & RADEON_RESET_VMC)
		srbm_soft_reset |= SOFT_RESET_VMC;

	if (!(rdev->flags & RADEON_IS_IGP)) {
		if (reset_mask & RADEON_RESET_MC)
			srbm_soft_reset |= SOFT_RESET_MC;
	}

	if (grbm_soft_reset) {
		tmp = RREG32(GRBM_SOFT_RESET);
		tmp |= grbm_soft_reset;
		dev_info(rdev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~grbm_soft_reset;
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);
	}

	if (srbm_soft_reset) {
		tmp = RREG32(SRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(rdev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);
	}

	/* Wait a little for things to settle down */
	udelay(50);

	evergreen_mc_resume(rdev, &save);
	udelay(50);

	cik_print_gpu_status_regs(rdev);
}

/**
 * cik_asic_reset - soft reset GPU
 *
 * @rdev: radeon_device pointer
 *
 * Look up which blocks are hung and attempt
 * to reset them.
 * Returns 0 for success.
 */
int cik_asic_reset(struct radeon_device *rdev)
{
	u32 reset_mask;

	reset_mask = cik_gpu_check_soft_reset(rdev);

	if (reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, true);

	cik_gpu_soft_reset(rdev, reset_mask);

	reset_mask = cik_gpu_check_soft_reset(rdev);

	if (!reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, false);

	return 0;
}

/**
 * cik_gfx_is_lockup - check if the 3D engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the 3D engine is locked up (CIK).
 * Returns true if the engine is locked, false if not.
 */
bool cik_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cik_gpu_check_soft_reset(rdev);

	if (!(reset_mask & (RADEON_RESET_GFX |
			    RADEON_RESET_COMPUTE |
			    RADEON_RESET_CP))) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force CP activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}

/**
 * cik_sdma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up (CIK).
 * Returns true if the engine appears to be locked up, false if not.
 */
bool cik_sdma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cik_gpu_check_soft_reset(rdev);
	u32 mask;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		mask = RADEON_RESET_DMA;
	else
		mask = RADEON_RESET_DMA1;

	if (!(reset_mask & mask)) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force ring activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}

/* MC */
/**
 * cik_mc_program - program the GPU memory controller
 *
 * @rdev: radeon_device pointer
 *
 * Set the location of vram, gart, and AGP in the GPU's
 * physical address space (CIK).
 */
static void cik_mc_program(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
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

	evergreen_mc_stop(rdev, &save);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Lockout access through VGA aperture*/
	WREG32(VGA_HDP_CONTROL, VGA_MEMORY_DISABLE);
	/* Update configuration */
	WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
	       rdev->mc.vram_start >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
	       rdev->mc.vram_end >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
	       rdev->vram_scratch.gpu_addr >> 12);
	tmp = ((rdev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((rdev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(MC_VM_FB_LOCATION, tmp);
	/* XXX double check these! */
	WREG32(HDP_NONSURFACE_BASE, (rdev->mc.vram_start >> 8));
	WREG32(HDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
	WREG32(HDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	WREG32(MC_VM_AGP_BASE, 0);
	WREG32(MC_VM_AGP_TOP, 0x0FFFFFFF);
	WREG32(MC_VM_AGP_BOT, 0x0FFFFFFF);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	evergreen_mc_resume(rdev, &save);
	/* we need to own VRAM, so turn off the VGA renderer here
	 * to stop it overwriting our objects */
	rv515_vga_render_disable(rdev);
}

/**
 * cik_mc_init - initialize the memory controller driver params
 *
 * @rdev: radeon_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space (CIK).
 * Returns 0 for success.
 */
static int cik_mc_init(struct radeon_device *rdev)
{
	u32 tmp;
	int chansize, numchan;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(MC_ARB_RAMCFG);
	if (tmp & CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(MC_SHARED_CHMAP);
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
	case 4:
		numchan = 3;
		break;
	case 5:
		numchan = 6;
		break;
	case 6:
		numchan = 10;
		break;
	case 7:
		numchan = 12;
		break;
	case 8:
		numchan = 16;
		break;
	}
	rdev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	/* size in MB on si */
	rdev->mc.mc_vram_size = RREG32(CONFIG_MEMSIZE) * 1024 * 1024;
	rdev->mc.real_vram_size = RREG32(CONFIG_MEMSIZE) * 1024 * 1024;
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	si_vram_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);

	return 0;
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the radeon vm/hsa code.
 */
/**
 * cik_pcie_gart_tlb_flush - gart tlb flush callback
 *
 * @rdev: radeon_device pointer
 *
 * Flush the TLB for the VMID 0 page table (CIK).
 */
void cik_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0);

	/* bits 0-15 are the VM contexts0-15 */
	WREG32(VM_INVALIDATE_REQUEST, 0x1);
}

/**
 * cik_pcie_gart_enable - gart enable
 *
 * @rdev: radeon_device pointer
 *
 * This sets up the TLBs, programs the page tables for VMID0,
 * sets up the hw for VMIDs 1-15 which are allocated on
 * demand, and sets up the global locations for the LDS, GDS,
 * and GPUVM for FSA64 clients (CIK).
 * Returns 0 for success, errors for failure.
 */
static int cik_pcie_gart_enable(struct radeon_device *rdev)
{
	int r, i;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	radeon_gart_restore(rdev);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL,
	       (0xA << 7) |
	       ENABLE_L1_TLB |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       ENABLE_ADVANCED_DRIVER_MODEL |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE |
	       ENABLE_L2_FRAGMENT_PROCESSING |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	/* setup context0 */
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT0_CNTL2, 0);
	WREG32(VM_CONTEXT0_CNTL, (ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				  RANGE_PROTECTION_FAULT_ENABLE_DEFAULT));

	WREG32(0x15D4, 0);
	WREG32(0x15D8, 0);
	WREG32(0x15DC, 0);

	/* empty context1-15 */
	/* FIXME start with 4G, once using 2 level pt switch to full
	 * vm size space
	 */
	/* set vm size, must be a multiple of 4 */
	WREG32(VM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(VM_CONTEXT1_PAGE_TABLE_END_ADDR, rdev->vm_manager.max_pfn);
	for (i = 1; i < 16; i++) {
		if (i < 8)
			WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
			       rdev->gart.table_addr >> 12);
		else
			WREG32(VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2),
			       rdev->gart.table_addr >> 12);
	}

	/* enable context1-15 */
	WREG32(VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT1_CNTL2, 4);
	WREG32(VM_CONTEXT1_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(1) |
				RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT |
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT |
				VALID_PROTECTION_FAULT_ENABLE_INTERRUPT |
				VALID_PROTECTION_FAULT_ENABLE_DEFAULT |
				READ_PROTECTION_FAULT_ENABLE_INTERRUPT |
				READ_PROTECTION_FAULT_ENABLE_DEFAULT |
				WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				WRITE_PROTECTION_FAULT_ENABLE_DEFAULT);

	/* TC cache setup ??? */
	WREG32(TC_CFG_L1_LOAD_POLICY0, 0);
	WREG32(TC_CFG_L1_LOAD_POLICY1, 0);
	WREG32(TC_CFG_L1_STORE_POLICY, 0);

	WREG32(TC_CFG_L2_LOAD_POLICY0, 0);
	WREG32(TC_CFG_L2_LOAD_POLICY1, 0);
	WREG32(TC_CFG_L2_STORE_POLICY0, 0);
	WREG32(TC_CFG_L2_STORE_POLICY1, 0);
	WREG32(TC_CFG_L2_ATOMIC_POLICY, 0);

	WREG32(TC_CFG_L1_VOLATILE, 0);
	WREG32(TC_CFG_L2_VOLATILE, 0);

	if (rdev->family == CHIP_KAVERI) {
		u32 tmp = RREG32(CHUB_CONTROL);
		tmp &= ~BYPASS_VM;
		WREG32(CHUB_CONTROL, tmp);
	}

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	for (i = 0; i < 16; i++) {
		WREG32(SRBM_GFX_CNTL, VMID(i));
		/* CP and shaders */
		WREG32(SH_MEM_CONFIG, 0);
		WREG32(SH_MEM_APE1_BASE, 1);
		WREG32(SH_MEM_APE1_LIMIT, 0);
		WREG32(SH_MEM_BASES, 0);
		/* SDMA GFX */
		WREG32(SDMA0_GFX_VIRTUAL_ADDR + SDMA0_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_APE1_CNTL + SDMA0_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_VIRTUAL_ADDR + SDMA1_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_APE1_CNTL + SDMA1_REGISTER_OFFSET, 0);
		/* XXX SDMA RLC - todo */
	}
	WREG32(SRBM_GFX_CNTL, 0);

	cik_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

/**
 * cik_pcie_gart_disable - gart disable
 *
 * @rdev: radeon_device pointer
 *
 * This disables all VM page table (CIK).
 */
static void cik_pcie_gart_disable(struct radeon_device *rdev)
{
	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL,
	       ENABLE_L2_FRAGMENT_PROCESSING |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	radeon_gart_table_vram_unpin(rdev);
}

/**
 * cik_pcie_gart_fini - vm fini callback
 *
 * @rdev: radeon_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void cik_pcie_gart_fini(struct radeon_device *rdev)
{
	cik_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

/* vm parser */
/**
 * cik_ib_parse - vm ib_parse callback
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer pointer
 *
 * CIK uses hw IB checking so this is a nop (CIK).
 */
int cik_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	return 0;
}

/*
 * vm
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the radeon vm/hsa code.
 */
/**
 * cik_vm_init - cik vm init callback
 *
 * @rdev: radeon_device pointer
 *
 * Inits cik specific vm parameters (number of VMs, base of vram for
 * VMIDs 1-15) (CIK).
 * Returns 0 for success.
 */
int cik_vm_init(struct radeon_device *rdev)
{
	/* number of VMs */
	rdev->vm_manager.nvm = 16;
	/* base offset of vram pages */
	if (rdev->flags & RADEON_IS_IGP) {
		u64 tmp = RREG32(MC_VM_FB_OFFSET);
		tmp <<= 22;
		rdev->vm_manager.vram_base_offset = tmp;
	} else
		rdev->vm_manager.vram_base_offset = 0;

	return 0;
}

/**
 * cik_vm_fini - cik vm fini callback
 *
 * @rdev: radeon_device pointer
 *
 * Tear down any asic specific VM setup (CIK).
 */
void cik_vm_fini(struct radeon_device *rdev)
{
}

/**
 * cik_vm_flush - cik vm flush using the CP
 *
 * @rdev: radeon_device pointer
 *
 * Update the page table base and flush the VM TLB
 * using the CP (CIK).
 */
void cik_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	if (vm == NULL)
		return;

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	if (vm->id < 8) {
		radeon_ring_write(ring,
				  (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2)) >> 2);
	} else {
		radeon_ring_write(ring,
				  (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm->id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, vm->pd_gpu_addr >> 12);

	/* update SH_MEM_* regs */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, VMID(vm->id));

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 6));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SH_MEM_BASES >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, 0); /* SH_MEM_BASES */
	radeon_ring_write(ring, 0); /* SH_MEM_CONFIG */
	radeon_ring_write(ring, 1); /* SH_MEM_APE1_BASE */
	radeon_ring_write(ring, 0); /* SH_MEM_APE1_LIMIT */

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, VMID(0));

	/* HDP flush */
	/* We should be using the WAIT_REG_MEM packet here like in
	 * cik_fence_ring_emit(), but it causes the CP to hang in this
	 * context...
	 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, HDP_MEM_COHERENCY_FLUSH_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);

	/* bits 0-15 are the VM contexts0-15 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 1 << vm->id);

	/* sync PFP to ME, otherwise we might get invalid PFP reads */
	radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
	radeon_ring_write(ring, 0x0);
}

/**
 * cik_vm_set_page - update the page tables using sDMA
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using CP or sDMA (CIK).
 */
void cik_vm_set_page(struct radeon_device *rdev,
		     struct radeon_ib *ib,
		     uint64_t pe,
		     uint64_t addr, unsigned count,
		     uint32_t incr, uint32_t flags)
{
	uint32_t r600_flags = cayman_vm_page_flags(rdev, flags);
	uint64_t value;
	unsigned ndw;

	if (rdev->asic->vm.pt_ring_index == RADEON_RING_TYPE_GFX_INDEX) {
		/* CP */
		while (count) {
			ndw = 2 + count * 2;
			if (ndw > 0x3FFE)
				ndw = 0x3FFE;

			ib->ptr[ib->length_dw++] = PACKET3(PACKET3_WRITE_DATA, ndw);
			ib->ptr[ib->length_dw++] = (WRITE_DATA_ENGINE_SEL(0) |
						    WRITE_DATA_DST_SEL(1));
			ib->ptr[ib->length_dw++] = pe;
			ib->ptr[ib->length_dw++] = upper_32_bits(pe);
			for (; ndw > 2; ndw -= 2, --count, pe += 8) {
				if (flags & RADEON_VM_PAGE_SYSTEM) {
					value = radeon_vm_map_gart(rdev, addr);
					value &= 0xFFFFFFFFFFFFF000ULL;
				} else if (flags & RADEON_VM_PAGE_VALID) {
					value = addr;
				} else {
					value = 0;
				}
				addr += incr;
				value |= r600_flags;
				ib->ptr[ib->length_dw++] = value;
				ib->ptr[ib->length_dw++] = upper_32_bits(value);
			}
		}
	} else {
		/* DMA */
		if (flags & RADEON_VM_PAGE_SYSTEM) {
			while (count) {
				ndw = count * 2;
				if (ndw > 0xFFFFE)
					ndw = 0xFFFFE;

				/* for non-physically contiguous pages (system) */
				ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
				ib->ptr[ib->length_dw++] = pe;
				ib->ptr[ib->length_dw++] = upper_32_bits(pe);
				ib->ptr[ib->length_dw++] = ndw;
				for (; ndw > 0; ndw -= 2, --count, pe += 8) {
					if (flags & RADEON_VM_PAGE_SYSTEM) {
						value = radeon_vm_map_gart(rdev, addr);
						value &= 0xFFFFFFFFFFFFF000ULL;
					} else if (flags & RADEON_VM_PAGE_VALID) {
						value = addr;
					} else {
						value = 0;
					}
					addr += incr;
					value |= r600_flags;
					ib->ptr[ib->length_dw++] = value;
					ib->ptr[ib->length_dw++] = upper_32_bits(value);
				}
			}
		} else {
			while (count) {
				ndw = count;
				if (ndw > 0x7FFFF)
					ndw = 0x7FFFF;

				if (flags & RADEON_VM_PAGE_VALID)
					value = addr;
				else
					value = 0;
				/* for physically contiguous pages (vram) */
				ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_GENERATE_PTE_PDE, 0, 0);
				ib->ptr[ib->length_dw++] = pe; /* dst addr */
				ib->ptr[ib->length_dw++] = upper_32_bits(pe);
				ib->ptr[ib->length_dw++] = r600_flags; /* mask */
				ib->ptr[ib->length_dw++] = 0;
				ib->ptr[ib->length_dw++] = value; /* value */
				ib->ptr[ib->length_dw++] = upper_32_bits(value);
				ib->ptr[ib->length_dw++] = incr; /* increment size */
				ib->ptr[ib->length_dw++] = 0;
				ib->ptr[ib->length_dw++] = ndw; /* number of entries */
				pe += ndw * 8;
				addr += ndw * incr;
				count -= ndw;
			}
		}
		while (ib->length_dw & 0x7)
			ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0);
	}
}

/**
 * cik_dma_vm_flush - cik vm flush using sDMA
 *
 * @rdev: radeon_device pointer
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (CIK).
 */
void cik_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm)
{
	struct radeon_ring *ring = &rdev->ring[ridx];
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(1) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(3)); /* == */
	u32 ref_and_mask;

	if (vm == NULL)
		return;

	if (ridx == R600_RING_TYPE_DMA_INDEX)
		ref_and_mask = SDMA0;
	else
		ref_and_mask = SDMA1;

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	if (vm->id < 8) {
		radeon_ring_write(ring, (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2)) >> 2);
	} else {
		radeon_ring_write(ring, (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm->id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, vm->pd_gpu_addr >> 12);

	/* update SH_MEM_* regs */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, VMID(vm->id));

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_BASES >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_CONFIG >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_APE1_BASE >> 2);
	radeon_ring_write(ring, 1);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_APE1_LIMIT >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, VMID(0));

	/* flush HDP */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	radeon_ring_write(ring, GPU_HDP_FLUSH_DONE);
	radeon_ring_write(ring, GPU_HDP_FLUSH_REQ);
	radeon_ring_write(ring, ref_and_mask); /* REFERENCE */
	radeon_ring_write(ring, ref_and_mask); /* MASK */
	radeon_ring_write(ring, (4 << 16) | 10); /* RETRY_COUNT, POLL_INTERVAL */

	/* flush TLB */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 1 << vm->id);
}

/*
 * RLC
 * The RLC is a multi-purpose microengine that handles a
 * variety of functions, the most important of which is
 * the interrupt controller.
 */
/**
 * cik_rlc_stop - stop the RLC ME
 *
 * @rdev: radeon_device pointer
 *
 * Halt the RLC ME (MicroEngine) (CIK).
 */
static void cik_rlc_stop(struct radeon_device *rdev)
{
	int i, j, k;
	u32 mask, tmp;

	tmp = RREG32(CP_INT_CNTL_RING0);
	tmp &= ~(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);

	RREG32(CB_CGTT_SCLK_CTRL);
	RREG32(CB_CGTT_SCLK_CTRL);
	RREG32(CB_CGTT_SCLK_CTRL);
	RREG32(CB_CGTT_SCLK_CTRL);

	tmp = RREG32(RLC_CGCG_CGLS_CTRL) & 0xfffffffc;
	WREG32(RLC_CGCG_CGLS_CTRL, tmp);

	WREG32(RLC_CNTL, 0);

	for (i = 0; i < rdev->config.cik.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.cik.max_sh_per_se; j++) {
			cik_select_se_sh(rdev, i, j);
			for (k = 0; k < rdev->usec_timeout; k++) {
				if (RREG32(RLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
		}
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	mask = SE_MASTER_BUSY_MASK | GC_MASTER_BUSY | TC0_MASTER_BUSY | TC1_MASTER_BUSY;
	for (k = 0; k < rdev->usec_timeout; k++) {
		if ((RREG32(RLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

/**
 * cik_rlc_start - start the RLC ME
 *
 * @rdev: radeon_device pointer
 *
 * Unhalt the RLC ME (MicroEngine) (CIK).
 */
static void cik_rlc_start(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(RLC_CNTL, RLC_ENABLE);

	tmp = RREG32(CP_INT_CNTL_RING0);
	tmp |= (CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);

	udelay(50);
}

/**
 * cik_rlc_resume - setup the RLC hw
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the RLC registers, load the ucode,
 * and start the RLC (CIK).
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_rlc_resume(struct radeon_device *rdev)
{
	u32 i, size;
	u32 clear_state_info[3];
	const __be32 *fw_data;

	if (!rdev->rlc_fw)
		return -EINVAL;

	switch (rdev->family) {
	case CHIP_BONAIRE:
	default:
		size = BONAIRE_RLC_UCODE_SIZE;
		break;
	case CHIP_KAVERI:
		size = KV_RLC_UCODE_SIZE;
		break;
	case CHIP_KABINI:
		size = KB_RLC_UCODE_SIZE;
		break;
	}

	cik_rlc_stop(rdev);

	WREG32(GRBM_SOFT_RESET, SOFT_RESET_RLC);
	RREG32(GRBM_SOFT_RESET);
	udelay(50);
	WREG32(GRBM_SOFT_RESET, 0);
	RREG32(GRBM_SOFT_RESET);
	udelay(50);

	WREG32(RLC_LB_CNTR_INIT, 0);
	WREG32(RLC_LB_CNTR_MAX, 0x00008000);

	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	WREG32(RLC_LB_INIT_CU_MASK, 0xffffffff);
	WREG32(RLC_LB_PARAMS, 0x00600408);
	WREG32(RLC_LB_CNTL, 0x80000004);

	WREG32(RLC_MC_CNTL, 0);
	WREG32(RLC_UCODE_CNTL, 0);

	fw_data = (const __be32 *)rdev->rlc_fw->data;
		WREG32(RLC_GPM_UCODE_ADDR, 0);
	for (i = 0; i < size; i++)
		WREG32(RLC_GPM_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(RLC_GPM_UCODE_ADDR, 0);

	/* XXX */
	clear_state_info[0] = 0;//upper_32_bits(rdev->rlc.save_restore_gpu_addr);
	clear_state_info[1] = 0;//rdev->rlc.save_restore_gpu_addr;
	clear_state_info[2] = 0;//cik_default_size;
	WREG32(RLC_GPM_SCRATCH_ADDR, 0x3d);
	for (i = 0; i < 3; i++)
		WREG32(RLC_GPM_SCRATCH_DATA, clear_state_info[i]);
	WREG32(RLC_DRIVER_DMA_STATUS, 0);

	cik_rlc_start(rdev);

	return 0;
}

/*
 * Interrupts
 * Starting with r6xx, interrupts are handled via a ring buffer.
 * Ring buffers are areas of GPU accessible memory that the GPU
 * writes interrupt vectors into and the host reads vectors out of.
 * There is a rptr (read pointer) that determines where the
 * host is currently reading, and a wptr (write pointer)
 * which determines where the GPU has written.  When the
 * pointers are equal, the ring is idle.  When the GPU
 * writes vectors to the ring buffer, it increments the
 * wptr.  When there is an interrupt, the host then starts
 * fetching commands and processing them until the pointers are
 * equal again at which point it updates the rptr.
 */

/**
 * cik_enable_interrupts - Enable the interrupt ring buffer
 *
 * @rdev: radeon_device pointer
 *
 * Enable the interrupt ring buffer (CIK).
 */
static void cik_enable_interrupts(struct radeon_device *rdev)
{
	u32 ih_cntl = RREG32(IH_CNTL);
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);

	ih_cntl |= ENABLE_INTR;
	ih_rb_cntl |= IH_RB_ENABLE;
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	rdev->ih.enabled = true;
}

/**
 * cik_disable_interrupts - Disable the interrupt ring buffer
 *
 * @rdev: radeon_device pointer
 *
 * Disable the interrupt ring buffer (CIK).
 */
static void cik_disable_interrupts(struct radeon_device *rdev)
{
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);
	u32 ih_cntl = RREG32(IH_CNTL);

	ih_rb_cntl &= ~IH_RB_ENABLE;
	ih_cntl &= ~ENABLE_INTR;
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	WREG32(IH_CNTL, ih_cntl);
	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);
	rdev->ih.enabled = false;
	rdev->ih.rptr = 0;
}

/**
 * cik_disable_interrupt_state - Disable all interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Clear all interrupt enable bits used by the driver (CIK).
 */
static void cik_disable_interrupt_state(struct radeon_device *rdev)
{
	u32 tmp;

	/* gfx ring */
	WREG32(CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	/* sdma */
	tmp = RREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	tmp = RREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	/* compute queues */
	WREG32(CP_ME1_PIPE0_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE1_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE2_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE3_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE0_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE1_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE2_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE3_INT_CNTL, 0);
	/* grbm */
	WREG32(GRBM_INT_CNTL, 0);
	/* vline/vblank, etc. */
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, 0);
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, 0);
	if (rdev->num_crtc >= 4) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, 0);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, 0);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, 0);
	}

	/* dac hotplug */
	WREG32(DAC_AUTODETECT_INT_CONTROL, 0);

	/* digital hotplug */
	tmp = RREG32(DC_HPD1_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD1_INT_CONTROL, tmp);
	tmp = RREG32(DC_HPD2_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD2_INT_CONTROL, tmp);
	tmp = RREG32(DC_HPD3_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD3_INT_CONTROL, tmp);
	tmp = RREG32(DC_HPD4_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD4_INT_CONTROL, tmp);
	tmp = RREG32(DC_HPD5_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD5_INT_CONTROL, tmp);
	tmp = RREG32(DC_HPD6_INT_CONTROL) & DC_HPDx_INT_POLARITY;
	WREG32(DC_HPD6_INT_CONTROL, tmp);

}

/**
 * cik_irq_init - init and enable the interrupt ring
 *
 * @rdev: radeon_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it (CIK).
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int cik_irq_init(struct radeon_device *rdev)
{
	int ret = 0;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;

	/* allocate ring */
	ret = r600_ih_ring_alloc(rdev);
	if (ret)
		return ret;

	/* disable irqs */
	cik_disable_interrupts(rdev);

	/* init rlc */
	ret = cik_rlc_resume(rdev);
	if (ret) {
		r600_ih_ring_fini(rdev);
		return ret;
	}

	/* setup interrupt control */
	/* XXX this should actually be a bus address, not an MC address. same on older asics */
	WREG32(INTERRUPT_CNTL2, rdev->ih.gpu_addr >> 8);
	interrupt_cntl = RREG32(INTERRUPT_CNTL);
	/* IH_DUMMY_RD_OVERRIDE=0 - dummy read disabled with msi, enabled without msi
	 * IH_DUMMY_RD_OVERRIDE=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl &= ~IH_DUMMY_RD_OVERRIDE;
	/* IH_REQ_NONSNOOP_EN=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl &= ~IH_REQ_NONSNOOP_EN;
	WREG32(INTERRUPT_CNTL, interrupt_cntl);

	WREG32(IH_RB_BASE, rdev->ih.gpu_addr >> 8);
	rb_bufsz = drm_order(rdev->ih.ring_size / 4);

	ih_rb_cntl = (IH_WPTR_OVERFLOW_ENABLE |
		      IH_WPTR_OVERFLOW_CLEAR |
		      (rb_bufsz << 1));

	if (rdev->wb.enabled)
		ih_rb_cntl |= IH_WPTR_WRITEBACK_ENABLE;

	/* set the writeback address whether it's enabled or not */
	WREG32(IH_RB_WPTR_ADDR_LO, (rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(IH_RB_WPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFF);

	WREG32(IH_RB_CNTL, ih_rb_cntl);

	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);

	/* Default settings for IH_CNTL (disabled at first) */
	ih_cntl = MC_WRREQ_CREDIT(0x10) | MC_WR_CLEAN_CNT(0x10) | MC_VMID(0);
	/* RPTR_REARM only works if msi's are enabled */
	if (rdev->msi_enabled)
		ih_cntl |= RPTR_REARM;
	WREG32(IH_CNTL, ih_cntl);

	/* force the active interrupt state to all disabled */
	cik_disable_interrupt_state(rdev);

	pci_set_master(rdev->pdev);

	/* enable irqs */
	cik_enable_interrupts(rdev);

	return ret;
}

/**
 * cik_irq_set - enable/disable interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Enable interrupt sources on the GPU (vblanks, hpd,
 * etc.) (CIK).
 * Returns 0 for success, errors for failure.
 */
int cik_irq_set(struct radeon_device *rdev)
{
	u32 cp_int_cntl = CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE |
		PRIV_INSTR_INT_ENABLE | PRIV_REG_INT_ENABLE;
	u32 crtc1 = 0, crtc2 = 0, crtc3 = 0, crtc4 = 0, crtc5 = 0, crtc6 = 0;
	u32 hpd1, hpd2, hpd3, hpd4, hpd5, hpd6;
	u32 grbm_int_cntl = 0;
	u32 dma_cntl, dma_cntl1;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		return -EINVAL;
	}
	/* don't enable anything if the ih is disabled */
	if (!rdev->ih.enabled) {
		cik_disable_interrupts(rdev);
		/* force the active interrupt state to all disabled */
		cik_disable_interrupt_state(rdev);
		return 0;
	}

	hpd1 = RREG32(DC_HPD1_INT_CONTROL) & ~DC_HPDx_INT_EN;
	hpd2 = RREG32(DC_HPD2_INT_CONTROL) & ~DC_HPDx_INT_EN;
	hpd3 = RREG32(DC_HPD3_INT_CONTROL) & ~DC_HPDx_INT_EN;
	hpd4 = RREG32(DC_HPD4_INT_CONTROL) & ~DC_HPDx_INT_EN;
	hpd5 = RREG32(DC_HPD5_INT_CONTROL) & ~DC_HPDx_INT_EN;
	hpd6 = RREG32(DC_HPD6_INT_CONTROL) & ~DC_HPDx_INT_EN;

	dma_cntl = RREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	dma_cntl1 = RREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;

	/* enable CP interrupts on all rings */
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int gfx\n");
		cp_int_cntl |= TIME_STAMP_INT_ENABLE;
	}
	/* TODO: compute queues! */
	/* CP_ME[1-2]_PIPE[0-3]_INT_CNTL */

	if (atomic_read(&rdev->irq.ring_int[R600_RING_TYPE_DMA_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int dma\n");
		dma_cntl |= TRAP_ENABLE;
	}

	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_DMA1_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int dma1\n");
		dma_cntl1 |= TRAP_ENABLE;
	}

	if (rdev->irq.crtc_vblank_int[0] ||
	    atomic_read(&rdev->irq.pflip[0])) {
		DRM_DEBUG("cik_irq_set: vblank 0\n");
		crtc1 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[1] ||
	    atomic_read(&rdev->irq.pflip[1])) {
		DRM_DEBUG("cik_irq_set: vblank 1\n");
		crtc2 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[2] ||
	    atomic_read(&rdev->irq.pflip[2])) {
		DRM_DEBUG("cik_irq_set: vblank 2\n");
		crtc3 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[3] ||
	    atomic_read(&rdev->irq.pflip[3])) {
		DRM_DEBUG("cik_irq_set: vblank 3\n");
		crtc4 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[4] ||
	    atomic_read(&rdev->irq.pflip[4])) {
		DRM_DEBUG("cik_irq_set: vblank 4\n");
		crtc5 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[5] ||
	    atomic_read(&rdev->irq.pflip[5])) {
		DRM_DEBUG("cik_irq_set: vblank 5\n");
		crtc6 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.hpd[0]) {
		DRM_DEBUG("cik_irq_set: hpd 1\n");
		hpd1 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[1]) {
		DRM_DEBUG("cik_irq_set: hpd 2\n");
		hpd2 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[2]) {
		DRM_DEBUG("cik_irq_set: hpd 3\n");
		hpd3 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[3]) {
		DRM_DEBUG("cik_irq_set: hpd 4\n");
		hpd4 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[4]) {
		DRM_DEBUG("cik_irq_set: hpd 5\n");
		hpd5 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[5]) {
		DRM_DEBUG("cik_irq_set: hpd 6\n");
		hpd6 |= DC_HPDx_INT_EN;
	}

	WREG32(CP_INT_CNTL_RING0, cp_int_cntl);

	WREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET, dma_cntl);
	WREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET, dma_cntl1);

	WREG32(GRBM_INT_CNTL, grbm_int_cntl);

	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, crtc1);
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, crtc2);
	if (rdev->num_crtc >= 4) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, crtc3);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, crtc4);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, crtc5);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, crtc6);
	}

	WREG32(DC_HPD1_INT_CONTROL, hpd1);
	WREG32(DC_HPD2_INT_CONTROL, hpd2);
	WREG32(DC_HPD3_INT_CONTROL, hpd3);
	WREG32(DC_HPD4_INT_CONTROL, hpd4);
	WREG32(DC_HPD5_INT_CONTROL, hpd5);
	WREG32(DC_HPD6_INT_CONTROL, hpd6);

	return 0;
}

/**
 * cik_irq_ack - ack interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Ack interrupt sources on the GPU (vblanks, hpd,
 * etc.) (CIK).  Certain interrupts sources are sw
 * generated and do not require an explicit ack.
 */
static inline void cik_irq_ack(struct radeon_device *rdev)
{
	u32 tmp;

	rdev->irq.stat_regs.cik.disp_int = RREG32(DISP_INTERRUPT_STATUS);
	rdev->irq.stat_regs.cik.disp_int_cont = RREG32(DISP_INTERRUPT_STATUS_CONTINUE);
	rdev->irq.stat_regs.cik.disp_int_cont2 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE2);
	rdev->irq.stat_regs.cik.disp_int_cont3 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE3);
	rdev->irq.stat_regs.cik.disp_int_cont4 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE4);
	rdev->irq.stat_regs.cik.disp_int_cont5 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE5);
	rdev->irq.stat_regs.cik.disp_int_cont6 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE6);

	if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VBLANK_INTERRUPT)
		WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VLINE_INTERRUPT)
		WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VLINE_ACK);
	if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VBLANK_INTERRUPT)
		WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VLINE_INTERRUPT)
		WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VLINE_ACK);

	if (rdev->num_crtc >= 4) {
		if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->num_crtc >= 6) {
		if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_INTERRUPT) {
		tmp = RREG32(DC_HPD1_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD1_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_INTERRUPT) {
		tmp = RREG32(DC_HPD2_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD2_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_INTERRUPT) {
		tmp = RREG32(DC_HPD3_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD3_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_INTERRUPT) {
		tmp = RREG32(DC_HPD4_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD4_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD5_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD6_INT_CONTROL, tmp);
	}
}

/**
 * cik_irq_disable - disable interrupts
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts on the hw (CIK).
 */
static void cik_irq_disable(struct radeon_device *rdev)
{
	cik_disable_interrupts(rdev);
	/* Wait and acknowledge irq */
	mdelay(1);
	cik_irq_ack(rdev);
	cik_disable_interrupt_state(rdev);
}

/**
 * cik_irq_disable - disable interrupts for suspend
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts and stop the RLC (CIK).
 * Used for suspend.
 */
static void cik_irq_suspend(struct radeon_device *rdev)
{
	cik_irq_disable(rdev);
	cik_rlc_stop(rdev);
}

/**
 * cik_irq_fini - tear down interrupt support
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts on the hw and free the IH ring
 * buffer (CIK).
 * Used for driver unload.
 */
static void cik_irq_fini(struct radeon_device *rdev)
{
	cik_irq_suspend(rdev);
	r600_ih_ring_fini(rdev);
}

/**
 * cik_get_ih_wptr - get the IH ring buffer wptr
 *
 * @rdev: radeon_device pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (CIK).  Also check for
 * ring buffer overflow and deal with it.
 * Used by cik_irq_process().
 * Returns the value of the wptr.
 */
static inline u32 cik_get_ih_wptr(struct radeon_device *rdev)
{
	u32 wptr, tmp;

	if (rdev->wb.enabled)
		wptr = le32_to_cpu(rdev->wb.wb[R600_WB_IH_WPTR_OFFSET/4]);
	else
		wptr = RREG32(IH_RB_WPTR);

	if (wptr & RB_OVERFLOW) {
		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 16). Hopefully
		 * this should allow us to catchup.
		 */
		dev_warn(rdev->dev, "IH ring buffer overflow (0x%08X, %d, %d)\n",
			wptr, rdev->ih.rptr, (wptr + 16) + rdev->ih.ptr_mask);
		rdev->ih.rptr = (wptr + 16) & rdev->ih.ptr_mask;
		tmp = RREG32(IH_RB_CNTL);
		tmp |= IH_WPTR_OVERFLOW_CLEAR;
		WREG32(IH_RB_CNTL, tmp);
	}
	return (wptr & rdev->ih.ptr_mask);
}

/*        CIK IV Ring
 * Each IV ring entry is 128 bits:
 * [7:0]    - interrupt source id
 * [31:8]   - reserved
 * [59:32]  - interrupt source data
 * [63:60]  - reserved
 * [71:64]  - RINGID
 *            CP:
 *            ME_ID [1:0], PIPE_ID[1:0], QUEUE_ID[2:0]
 *            QUEUE_ID - for compute, which of the 8 queues owned by the dispatcher
 *                     - for gfx, hw shader state (0=PS...5=LS, 6=CS)
 *            ME_ID - 0 = gfx, 1 = first 4 CS pipes, 2 = second 4 CS pipes
 *            PIPE_ID - ME0 0=3D
 *                    - ME1&2 compute dispatcher (4 pipes each)
 *            SDMA:
 *            INSTANCE_ID [1:0], QUEUE_ID[1:0]
 *            INSTANCE_ID - 0 = sdma0, 1 = sdma1
 *            QUEUE_ID - 0 = gfx, 1 = rlc0, 2 = rlc1
 * [79:72]  - VMID
 * [95:80]  - PASID
 * [127:96] - reserved
 */
/**
 * cik_irq_process - interrupt handler
 *
 * @rdev: radeon_device pointer
 *
 * Interrupt hander (CIK).  Walk the IH ring,
 * ack interrupts and schedule work to handle
 * interrupt events.
 * Returns irq process return code.
 */
int cik_irq_process(struct radeon_device *rdev)
{
	u32 wptr;
	u32 rptr;
	u32 src_id, src_data, ring_id;
	u8 me_id, pipe_id, queue_id;
	u32 ring_index;
	bool queue_hotplug = false;
	bool queue_reset = false;

	if (!rdev->ih.enabled || rdev->shutdown)
		return IRQ_NONE;

	wptr = cik_get_ih_wptr(rdev);

restart_ih:
	/* is somebody else already processing irqs? */
	if (atomic_xchg(&rdev->ih.lock, 1))
		return IRQ_NONE;

	rptr = rdev->ih.rptr;
	DRM_DEBUG("cik_irq_process start: rptr %d, wptr %d\n", rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	/* display interrupts */
	cik_irq_ack(rdev);

	while (rptr != wptr) {
		/* wptr/rptr are in bytes! */
		ring_index = rptr / 4;
		src_id =  le32_to_cpu(rdev->ih.ring[ring_index]) & 0xff;
		src_data = le32_to_cpu(rdev->ih.ring[ring_index + 1]) & 0xfffffff;
		ring_id = le32_to_cpu(rdev->ih.ring[ring_index + 2]) & 0xff;

		switch (src_id) {
		case 1: /* D1 vblank/vline */
			switch (src_data) {
			case 0: /* D1 vblank */
				if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[0]) {
						drm_handle_vblank(rdev->ddev, 0);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[0]))
						radeon_crtc_handle_flip(rdev, 0);
					rdev->irq.stat_regs.cik.disp_int &= ~LB_D1_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D1 vblank\n");
				}
				break;
			case 1: /* D1 vline */
				if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int &= ~LB_D1_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D1 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 2: /* D2 vblank/vline */
			switch (src_data) {
			case 0: /* D2 vblank */
				if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[1]) {
						drm_handle_vblank(rdev->ddev, 1);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[1]))
						radeon_crtc_handle_flip(rdev, 1);
					rdev->irq.stat_regs.cik.disp_int_cont &= ~LB_D2_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D2 vblank\n");
				}
				break;
			case 1: /* D2 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont &= ~LB_D2_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D2 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 3: /* D3 vblank/vline */
			switch (src_data) {
			case 0: /* D3 vblank */
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[2]) {
						drm_handle_vblank(rdev->ddev, 2);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[2]))
						radeon_crtc_handle_flip(rdev, 2);
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~LB_D3_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D3 vblank\n");
				}
				break;
			case 1: /* D3 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~LB_D3_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D3 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 4: /* D4 vblank/vline */
			switch (src_data) {
			case 0: /* D4 vblank */
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[3]) {
						drm_handle_vblank(rdev->ddev, 3);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[3]))
						radeon_crtc_handle_flip(rdev, 3);
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~LB_D4_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D4 vblank\n");
				}
				break;
			case 1: /* D4 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~LB_D4_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D4 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 5: /* D5 vblank/vline */
			switch (src_data) {
			case 0: /* D5 vblank */
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[4]) {
						drm_handle_vblank(rdev->ddev, 4);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[4]))
						radeon_crtc_handle_flip(rdev, 4);
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~LB_D5_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D5 vblank\n");
				}
				break;
			case 1: /* D5 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~LB_D5_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D5 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 6: /* D6 vblank/vline */
			switch (src_data) {
			case 0: /* D6 vblank */
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[5]) {
						drm_handle_vblank(rdev->ddev, 5);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[5]))
						radeon_crtc_handle_flip(rdev, 5);
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~LB_D6_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D6 vblank\n");
				}
				break;
			case 1: /* D6 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~LB_D6_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D6 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 42: /* HPD hotplug */
			switch (src_data) {
			case 0:
				if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int &= ~DC_HPD1_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD1\n");
				}
				break;
			case 1:
				if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont &= ~DC_HPD2_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD2\n");
				}
				break;
			case 2:
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~DC_HPD3_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD3\n");
				}
				break;
			case 3:
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~DC_HPD4_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD4\n");
				}
				break;
			case 4:
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~DC_HPD5_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD5\n");
				}
				break;
			case 5:
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~DC_HPD6_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD6\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 146:
		case 147:
			dev_err(rdev->dev, "GPU fault detected: %d 0x%08x\n", src_id, src_data);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
				RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
				RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));
			/* reset addr and status */
			WREG32_P(VM_CONTEXT1_CNTL2, 1, ~1);
			break;
		case 176: /* GFX RB CP_INT */
		case 177: /* GFX IB CP_INT */
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
			break;
		case 181: /* CP EOP event */
			DRM_DEBUG("IH: CP EOP\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
				break;
			case 1:
				/* XXX compute */
				break;
			case 2:
				/* XXX compute */
				break;
			}
			break;
		case 184: /* CP Privileged reg access */
			DRM_ERROR("Illegal register access in command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				/* This results in a full GPU reset, but all we need to do is soft
				 * reset the CP for gfx
				 */
				queue_reset = true;
				break;
			case 1:
				/* XXX compute */
				break;
			case 2:
				/* XXX compute */
				break;
			}
			break;
		case 185: /* CP Privileged inst */
			DRM_ERROR("Illegal instruction in command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				/* This results in a full GPU reset, but all we need to do is soft
				 * reset the CP for gfx
				 */
				queue_reset = true;
				break;
			case 1:
				/* XXX compute */
				break;
			case 2:
				/* XXX compute */
				break;
			}
			break;
		case 224: /* SDMA trap event */
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x3) >> 0;
			queue_id = (ring_id & 0xc) >> 2;
			DRM_DEBUG("IH: SDMA trap\n");
			switch (me_id) {
			case 0:
				switch (queue_id) {
				case 0:
					radeon_fence_process(rdev, R600_RING_TYPE_DMA_INDEX);
					break;
				case 1:
					/* XXX compute */
					break;
				case 2:
					/* XXX compute */
					break;
				}
				break;
			case 1:
				switch (queue_id) {
				case 0:
					radeon_fence_process(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
					break;
				case 1:
					/* XXX compute */
					break;
				case 2:
					/* XXX compute */
					break;
				}
				break;
			}
			break;
		case 241: /* SDMA Privileged inst */
		case 247: /* SDMA Privileged inst */
			DRM_ERROR("Illegal instruction in SDMA command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x3) >> 0;
			queue_id = (ring_id & 0xc) >> 2;
			switch (me_id) {
			case 0:
				switch (queue_id) {
				case 0:
					queue_reset = true;
					break;
				case 1:
					/* XXX compute */
					queue_reset = true;
					break;
				case 2:
					/* XXX compute */
					queue_reset = true;
					break;
				}
				break;
			case 1:
				switch (queue_id) {
				case 0:
					queue_reset = true;
					break;
				case 1:
					/* XXX compute */
					queue_reset = true;
					break;
				case 2:
					/* XXX compute */
					queue_reset = true;
					break;
				}
				break;
			}
			break;
		case 233: /* GUI IDLE */
			DRM_DEBUG("IH: GUI idle\n");
			break;
		default:
			DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
			break;
		}

		/* wptr/rptr are in bytes! */
		rptr += 16;
		rptr &= rdev->ih.ptr_mask;
	}
	if (queue_hotplug)
		schedule_work(&rdev->hotplug_work);
	if (queue_reset)
		schedule_work(&rdev->reset_work);
	rdev->ih.rptr = rptr;
	WREG32(IH_RB_RPTR, rdev->ih.rptr);
	atomic_set(&rdev->ih.lock, 0);

	/* make sure wptr hasn't changed while processing */
	wptr = cik_get_ih_wptr(rdev);
	if (wptr != rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/*
 * startup/shutdown callbacks
 */
/**
 * cik_startup - program the asic to a functional state
 *
 * @rdev: radeon_device pointer
 *
 * Programs the asic to a functional state (CIK).
 * Called by cik_init() and cik_resume().
 * Returns 0 for success, error for failure.
 */
static int cik_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (rdev->flags & RADEON_IS_IGP) {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
		    !rdev->mec_fw || !rdev->sdma_fw || !rdev->rlc_fw) {
			r = cik_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	} else {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
		    !rdev->mec_fw || !rdev->sdma_fw || !rdev->rlc_fw ||
		    !rdev->mc_fw) {
			r = cik_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}

		r = ci_mc_load_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	cik_mc_program(rdev);
	r = cik_pcie_gart_enable(rdev);
	if (r)
		return r;
	cik_gpu_init(rdev);

	/* allocate rlc buffers */
	r = si_rlc_init(rdev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
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

	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_DMA_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = cik_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	cik_irq_set(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     CP_RB0_RPTR, CP_RB0_WPTR,
			     0, 0xfffff, RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     SDMA0_GFX_RB_RPTR + SDMA0_REGISTER_OFFSET,
			     SDMA0_GFX_RB_WPTR + SDMA0_REGISTER_OFFSET,
			     2, 0xfffffffc, SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, CAYMAN_WB_DMA1_RPTR_OFFSET,
			     SDMA0_GFX_RB_RPTR + SDMA1_REGISTER_OFFSET,
			     SDMA0_GFX_RB_WPTR + SDMA1_REGISTER_OFFSET,
			     2, 0xfffffffc, SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	if (r)
		return r;

	r = cik_cp_resume(rdev);
	if (r)
		return r;

	r = cik_sdma_resume(rdev);
	if (r)
		return r;

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_vm_manager_init(rdev);
	if (r) {
		dev_err(rdev->dev, "vm manager initialization failed (%d).\n", r);
		return r;
	}

	return 0;
}

/**
 * cik_resume - resume the asic to a functional state
 *
 * @rdev: radeon_device pointer
 *
 * Programs the asic to a functional state (CIK).
 * Called at resume.
 * Returns 0 for success, error for failure.
 */
int cik_resume(struct radeon_device *rdev)
{
	int r;

	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	rdev->accel_working = true;
	r = cik_startup(rdev);
	if (r) {
		DRM_ERROR("cik startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}

	return r;

}

/**
 * cik_suspend - suspend the asic
 *
 * @rdev: radeon_device pointer
 *
 * Bring the chip into a state suitable for suspend (CIK).
 * Called at suspend.
 * Returns 0 for success.
 */
int cik_suspend(struct radeon_device *rdev)
{
	radeon_vm_manager_fini(rdev);
	cik_cp_enable(rdev, false);
	cik_sdma_enable(rdev, false);
	cik_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	cik_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
/**
 * cik_init - asic specific driver and hw init
 *
 * @rdev: radeon_device pointer
 *
 * Setup asic specific driver variables and program the hw
 * to a functional state (CIK).
 * Called at driver startup.
 * Returns 0 for success, errors for failure.
 */
int cik_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	/* Read BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	/* Must be an ATOMBIOS */
	if (!rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting atombios for cayman GPU\n");
		return -EINVAL;
	}
	r = radeon_atombios_init(rdev);
	if (r)
		return r;

	/* Post card if necessary */
	if (!radeon_card_posted(rdev)) {
		if (!rdev->bios) {
			dev_err(rdev->dev, "Card not posted and no BIOS - ignoring\n");
			return -EINVAL;
		}
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* Initialize scratch registers */
	cik_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);

	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;

	/* initialize memory controller */
	r = cik_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 256 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 256 * 1024);

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = cik_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		cik_cp_fini(rdev);
		cik_sdma_fini(rdev);
		cik_irq_fini(rdev);
		si_rlc_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		cik_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 */
	if (!rdev->mc_fw && !(rdev->flags & RADEON_IS_IGP)) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * cik_fini - asic specific driver and hw fini
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the asic specific driver variables and program the hw
 * to an idle state (CIK).
 * Called at driver unload.
 */
void cik_fini(struct radeon_device *rdev)
{
	cik_cp_fini(rdev);
	cik_sdma_fini(rdev);
	cik_irq_fini(rdev);
	si_rlc_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	cik_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/* display watermark setup */
/**
 * dce8_line_buffer_adjust - Set up the line buffer
 *
 * @rdev: radeon_device pointer
 * @radeon_crtc: the selected display controller
 * @mode: the current display mode on the selected display
 * controller
 *
 * Setup up the line buffer allocation for
 * the selected display controller (CIK).
 * Returns the line buffer size in pixels.
 */
static u32 dce8_line_buffer_adjust(struct radeon_device *rdev,
				   struct radeon_crtc *radeon_crtc,
				   struct drm_display_mode *mode)
{
	u32 tmp;

	/*
	 * Line Buffer Setup
	 * There are 6 line buffers, one for each display controllers.
	 * There are 3 partitions per LB. Select the number of partitions
	 * to enable based on the display width.  For display widths larger
	 * than 4096, you need use to use 2 display controllers and combine
	 * them using the stereo blender.
	 */
	if (radeon_crtc->base.enabled && mode) {
		if (mode->crtc_hdisplay < 1920)
			tmp = 1;
		else if (mode->crtc_hdisplay < 2560)
			tmp = 2;
		else if (mode->crtc_hdisplay < 4096)
			tmp = 0;
		else {
			DRM_DEBUG_KMS("Mode too big for LB!\n");
			tmp = 0;
		}
	} else
		tmp = 1;

	WREG32(LB_MEMORY_CTRL + radeon_crtc->crtc_offset,
	       LB_MEMORY_CONFIG(tmp) | LB_MEMORY_SIZE(0x6B0));

	if (radeon_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 1:
			return 1920 * 2;
		case 2:
			return 2560 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

/**
 * cik_get_number_of_dram_channels - get the number of dram channels
 *
 * @rdev: radeon_device pointer
 *
 * Look up the number of video ram channels (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the number of dram channels
 */
static u32 cik_get_number_of_dram_channels(struct radeon_device *rdev)
{
	u32 tmp = RREG32(MC_SHARED_CHMAP);

	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 8;
	case 4:
		return 3;
	case 5:
		return 6;
	case 6:
		return 10;
	case 7:
		return 12;
	case 8:
		return 16;
	}
}

struct dce8_wm_params {
	u32 dram_channels; /* number of dram channels */
	u32 yclk;          /* bandwidth per dram data pin in kHz */
	u32 sclk;          /* engine clock in kHz */
	u32 disp_clk;      /* display clock in kHz */
	u32 src_width;     /* viewport width */
	u32 active_time;   /* active display time in ns */
	u32 blank_time;    /* blank time in ns */
	bool interlaced;    /* mode is interlaced */
	fixed20_12 vsc;    /* vertical scale ratio */
	u32 num_heads;     /* number of active crtcs */
	u32 bytes_per_pixel; /* bytes per pixel display + overlay */
	u32 lb_size;       /* line buffer allocated to pipe */
	u32 vtaps;         /* vertical scaler taps */
};

/**
 * dce8_dram_bandwidth - get the dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the raw dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth in MBytes/s
 */
static u32 dce8_dram_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate raw DRAM Bandwidth */
	fixed20_12 dram_efficiency; /* 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	dram_efficiency.full = dfixed_const(7);
	dram_efficiency.full = dfixed_div(dram_efficiency, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, dram_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_dram_bandwidth_for_display - get the dram bandwidth for display
 *
 * @wm: watermark calculation data
 *
 * Calculate the dram bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth for display in MBytes/s
 */
static u32 dce8_dram_bandwidth_for_display(struct dce8_wm_params *wm)
{
	/* Calculate DRAM Bandwidth and the part allocated to display. */
	fixed20_12 disp_dram_allocation; /* 0.3 to 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	disp_dram_allocation.full = dfixed_const(3); /* XXX worse case value 0.3 */
	disp_dram_allocation.full = dfixed_div(disp_dram_allocation, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, disp_dram_allocation);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_data_return_bandwidth - get the data return bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the data return bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the data return bandwidth in MBytes/s
 */
static u32 dce8_data_return_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the display Data return Bandwidth */
	fixed20_12 return_efficiency; /* 0.8 */
	fixed20_12 sclk, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(10);
	return_efficiency.full = dfixed_const(8);
	return_efficiency.full = dfixed_div(return_efficiency, a);
	a.full = dfixed_const(32);
	bandwidth.full = dfixed_mul(a, sclk);
	bandwidth.full = dfixed_mul(bandwidth, return_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_dmif_request_bandwidth - get the dmif bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the dmif bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dmif bandwidth in MBytes/s
 */
static u32 dce8_dmif_request_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, bandwidth;
	fixed20_12 a, b;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(32);
	b.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	bandwidth.full = dfixed_mul(b, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_available_bandwidth - get the min available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the min available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the min available bandwidth in MBytes/s
 */
static u32 dce8_available_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce8_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce8_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce8_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

/**
 * dce8_average_bandwidth - get the average available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the average available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the average available bandwidth in MBytes/s
 */
static u32 dce8_average_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the display mode Average Bandwidth
	 * DisplayMode should contain the source and destination dimensions,
	 * timing, etc.
	 */
	fixed20_12 bpp;
	fixed20_12 line_time;
	fixed20_12 src_width;
	fixed20_12 bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	line_time.full = dfixed_const(wm->active_time + wm->blank_time);
	line_time.full = dfixed_div(line_time, a);
	bpp.full = dfixed_const(wm->bytes_per_pixel);
	src_width.full = dfixed_const(wm->src_width);
	bandwidth.full = dfixed_mul(src_width, bpp);
	bandwidth.full = dfixed_mul(bandwidth, wm->vsc);
	bandwidth.full = dfixed_div(bandwidth, line_time);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_latency_watermark - get the latency watermark
 *
 * @wm: watermark calculation data
 *
 * Calculate the latency watermark (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the latency watermark in ns
 */
static u32 dce8_latency_watermark(struct dce8_wm_params *wm)
{
	/* First calculate the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce8_available_bandwidth(wm);
	u32 worst_chunk_return_time = (512 * 8 * 1000) / available_bandwidth;
	u32 cursor_line_pair_return_time = (128 * 4 * 1000) / available_bandwidth;
	u32 dc_latency = 40000000 / wm->disp_clk; /* dc pipe latency */
	u32 other_heads_data_return_time = ((wm->num_heads + 1) * worst_chunk_return_time) +
		(wm->num_heads * cursor_line_pair_return_time);
	u32 latency = mc_latency + other_heads_data_return_time + dc_latency;
	u32 max_src_lines_per_dst_line, lb_fill_bw, line_fill_time;
	u32 tmp, dmif_size = 12288;
	fixed20_12 a, b, c;

	if (wm->num_heads == 0)
		return 0;

	a.full = dfixed_const(2);
	b.full = dfixed_const(1);
	if ((wm->vsc.full > a.full) ||
	    ((wm->vsc.full > b.full) && (wm->vtaps >= 3)) ||
	    (wm->vtaps >= 5) ||
	    ((wm->vsc.full >= a.full) && wm->interlaced))
		max_src_lines_per_dst_line = 4;
	else
		max_src_lines_per_dst_line = 2;

	a.full = dfixed_const(available_bandwidth);
	b.full = dfixed_const(wm->num_heads);
	a.full = dfixed_div(a, b);

	b.full = dfixed_const(mc_latency + 512);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(b, c);

	c.full = dfixed_const(dmif_size);
	b.full = dfixed_div(c, b);

	tmp = min(dfixed_trunc(a), dfixed_trunc(b));

	b.full = dfixed_const(1000);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(c, b);
	c.full = dfixed_const(wm->bytes_per_pixel);
	b.full = dfixed_mul(b, c);

	lb_fill_bw = min(tmp, dfixed_trunc(b));

	a.full = dfixed_const(max_src_lines_per_dst_line * wm->src_width * wm->bytes_per_pixel);
	b.full = dfixed_const(1000);
	c.full = dfixed_const(lb_fill_bw);
	b.full = dfixed_div(c, b);
	a.full = dfixed_div(a, b);
	line_fill_time = dfixed_trunc(a);

	if (line_fill_time < wm->active_time)
		return latency;
	else
		return latency + (line_fill_time - wm->active_time);

}

/**
 * dce8_average_bandwidth_vs_dram_bandwidth_for_display - check
 * average and available dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_average_bandwidth_vs_dram_bandwidth_for_display(struct dce8_wm_params *wm)
{
	if (dce8_average_bandwidth(wm) <=
	    (dce8_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce8_average_bandwidth_vs_available_bandwidth - check
 * average and available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * available bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_average_bandwidth_vs_available_bandwidth(struct dce8_wm_params *wm)
{
	if (dce8_average_bandwidth(wm) <=
	    (dce8_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce8_check_latency_hiding - check latency hiding
 *
 * @wm: watermark calculation data
 *
 * Check latency hiding (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_check_latency_hiding(struct dce8_wm_params *wm)
{
	u32 lb_partitions = wm->lb_size / wm->src_width;
	u32 line_time = wm->active_time + wm->blank_time;
	u32 latency_tolerant_lines;
	u32 latency_hiding;
	fixed20_12 a;

	a.full = dfixed_const(1);
	if (wm->vsc.full > a.full)
		latency_tolerant_lines = 1;
	else {
		if (lb_partitions <= (wm->vtaps + 1))
			latency_tolerant_lines = 1;
		else
			latency_tolerant_lines = 2;
	}

	latency_hiding = (latency_tolerant_lines * line_time + wm->blank_time);

	if (dce8_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

/**
 * dce8_program_watermarks - program display watermarks
 *
 * @rdev: radeon_device pointer
 * @radeon_crtc: the selected display controller
 * @lb_size: line buffer size
 * @num_heads: number of display controllers in use
 *
 * Calculate and program the display watermarks for the
 * selected display controller (CIK).
 */
static void dce8_program_watermarks(struct radeon_device *rdev,
				    struct radeon_crtc *radeon_crtc,
				    u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &radeon_crtc->base.mode;
	struct dce8_wm_params wm;
	u32 pixel_period;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 tmp, wm_mask;

	if (radeon_crtc->base.enabled && num_heads && mode) {
		pixel_period = 1000000 / (u32)mode->clock;
		line_time = min((u32)mode->crtc_htotal * pixel_period, (u32)65535);

		wm.yclk = rdev->pm.current_mclk * 10;
		wm.sclk = rdev->pm.current_sclk * 10;
		wm.disp_clk = mode->clock;
		wm.src_width = mode->crtc_hdisplay;
		wm.active_time = mode->crtc_hdisplay * pixel_period;
		wm.blank_time = line_time - wm.active_time;
		wm.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm.interlaced = true;
		wm.vsc = radeon_crtc->vsc;
		wm.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm.vtaps = 2;
		wm.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm.lb_size = lb_size;
		wm.dram_channels = cik_get_number_of_dram_channels(rdev);
		wm.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce8_latency_watermark(&wm), (u32)65535);
		/* set for low clocks */
		/* wm.yclk = low clk; wm.sclk = low clk */
		latency_watermark_b = min(dce8_latency_watermark(&wm), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce8_average_bandwidth_vs_dram_bandwidth_for_display(&wm) ||
		    !dce8_average_bandwidth_vs_available_bandwidth(&wm) ||
		    !dce8_check_latency_hiding(&wm) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}
	}

	/* select wm A */
	wm_mask = RREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset);
	tmp = wm_mask;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_a) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* select wm B */
	tmp = RREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_b) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* restore original selection */
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, wm_mask);
}

/**
 * dce8_bandwidth_update - program display watermarks
 *
 * @rdev: radeon_device pointer
 *
 * Calculate and program the display watermarks and line
 * buffer allocation (CIK).
 */
void dce8_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	radeon_update_display_priority(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < rdev->num_crtc; i++) {
		mode = &rdev->mode_info.crtcs[i]->base.mode;
		lb_size = dce8_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i], mode);
		dce8_program_watermarks(rdev, rdev->mode_info.crtcs[i], lb_size, num_heads);
	}
}

/**
 * cik_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @rdev: radeon_device pointer
 *
 * Fetches a GPU clock counter snapshot (SI).
 * Returns the 64 bit clock counter snapshot.
 */
uint64_t cik_get_gpu_clock_counter(struct radeon_device *rdev)
{
	uint64_t clock;

	mutex_lock(&rdev->gpu_clock_mutex);
	WREG32(RLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_LSB) |
	        ((uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&rdev->gpu_clock_mutex);
	return clock;
}

