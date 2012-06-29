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

MODULE_FIRMWARE("radeon/BONAIRE_pfp.bin");
MODULE_FIRMWARE("radeon/BONAIRE_me.bin");
MODULE_FIRMWARE("radeon/BONAIRE_ce.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mec.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mc.bin");
MODULE_FIRMWARE("radeon/BONAIRE_rlc.bin");
MODULE_FIRMWARE("radeon/KAVERI_pfp.bin");
MODULE_FIRMWARE("radeon/KAVERI_me.bin");
MODULE_FIRMWARE("radeon/KAVERI_ce.bin");
MODULE_FIRMWARE("radeon/KAVERI_mec.bin");
MODULE_FIRMWARE("radeon/KAVERI_rlc.bin");
MODULE_FIRMWARE("radeon/KABINI_pfp.bin");
MODULE_FIRMWARE("radeon/KABINI_me.bin");
MODULE_FIRMWARE("radeon/KABINI_ce.bin");
MODULE_FIRMWARE("radeon/KABINI_mec.bin");
MODULE_FIRMWARE("radeon/KABINI_rlc.bin");

extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void si_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);

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
		mec_req_size, rlc_req_size, mc_req_size;
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
		break;
	case CHIP_KAVERI:
		chip_name = "KAVERI";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KV_RLC_UCODE_SIZE * 4;
		break;
	case CHIP_KABINI:
		chip_name = "KABINI";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KB_RLC_UCODE_SIZE * 4;
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

/**
 * cik_gpu_is_lockup - check if the 3D engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the 3D engine is locked up (CIK).
 * Returns true if the engine is locked, false if not.
 */
bool cik_gpu_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 srbm_status, srbm_status2;
	u32 grbm_status, grbm_status2;
	u32 grbm_status_se0, grbm_status_se1, grbm_status_se2, grbm_status_se3;

	srbm_status = RREG32(SRBM_STATUS);
	srbm_status2 = RREG32(SRBM_STATUS2);
	grbm_status = RREG32(GRBM_STATUS);
	grbm_status2 = RREG32(GRBM_STATUS2);
	grbm_status_se0 = RREG32(GRBM_STATUS_SE0);
	grbm_status_se1 = RREG32(GRBM_STATUS_SE1);
	grbm_status_se2 = RREG32(GRBM_STATUS_SE2);
	grbm_status_se3 = RREG32(GRBM_STATUS_SE3);
	if (!(grbm_status & GUI_ACTIVE)) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force CP activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}

/**
 * cik_gfx_gpu_soft_reset - soft reset the 3D engine and CPG
 *
 * @rdev: radeon_device pointer
 *
 * Soft reset the GFX engine and CPG blocks (CIK).
 * XXX: deal with reseting RLC and CPF
 * Returns 0 for success.
 */
static int cik_gfx_gpu_soft_reset(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 grbm_reset = 0;

	if (!(RREG32(GRBM_STATUS) & GUI_ACTIVE))
		return 0;

	dev_info(rdev->dev, "GPU GFX softreset \n");
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
	evergreen_mc_stop(rdev, &save);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	/* reset all the gfx block and all CPG blocks */
	grbm_reset = SOFT_RESET_CPG | SOFT_RESET_GFX;

	dev_info(rdev->dev, "  GRBM_SOFT_RESET=0x%08X\n", grbm_reset);
	WREG32(GRBM_SOFT_RESET, grbm_reset);
	(void)RREG32(GRBM_SOFT_RESET);
	udelay(50);
	WREG32(GRBM_SOFT_RESET, 0);
	(void)RREG32(GRBM_SOFT_RESET);
	/* Wait a little for things to settle down */
	udelay(50);
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
	evergreen_mc_resume(rdev, &save);
	return 0;
}

/**
 * cik_compute_gpu_soft_reset - soft reset CPC
 *
 * @rdev: radeon_device pointer
 *
 * Soft reset the CPC blocks (CIK).
 * XXX: deal with reseting RLC and CPF
 * Returns 0 for success.
 */
static int cik_compute_gpu_soft_reset(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 grbm_reset = 0;

	dev_info(rdev->dev, "GPU compute softreset \n");
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
	evergreen_mc_stop(rdev, &save);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Disable CP parsing/prefetching */
	WREG32(CP_MEC_CNTL, MEC_ME1_HALT | MEC_ME2_HALT);

	/* reset all the CPC blocks */
	grbm_reset = SOFT_RESET_CPG;

	dev_info(rdev->dev, "  GRBM_SOFT_RESET=0x%08X\n", grbm_reset);
	WREG32(GRBM_SOFT_RESET, grbm_reset);
	(void)RREG32(GRBM_SOFT_RESET);
	udelay(50);
	WREG32(GRBM_SOFT_RESET, 0);
	(void)RREG32(GRBM_SOFT_RESET);
	/* Wait a little for things to settle down */
	udelay(50);
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
	evergreen_mc_resume(rdev, &save);
	return 0;
}

/**
 * cik_asic_reset - soft reset compute and gfx
 *
 * @rdev: radeon_device pointer
 *
 * Soft reset the CPC blocks (CIK).
 * XXX: make this more fine grained and only reset
 * what is necessary.
 * Returns 0 for success.
 */
int cik_asic_reset(struct radeon_device *rdev)
{
	int r;

	r = cik_compute_gpu_soft_reset(rdev);
	if (r)
		dev_info(rdev->dev, "Compute reset failed!\n");

	return cik_gfx_gpu_soft_reset(rdev);
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
		WREG32(SH_MEM_CONFIG, 0);
		WREG32(SH_MEM_APE1_BASE, 1);
		WREG32(SH_MEM_APE1_LIMIT, 0);
		WREG32(SH_MEM_BASES, 0);
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

