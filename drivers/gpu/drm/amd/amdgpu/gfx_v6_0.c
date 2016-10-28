/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "amdgpu_gfx.h"
#include "amdgpu_ucode.h"
#include "si/clearstate_si.h"
#include "si/sid.h"

#define GFX6_NUM_GFX_RINGS     1
#define GFX6_NUM_COMPUTE_RINGS 2
#define STATIC_PER_CU_PG_ENABLE                    (1 << 3)
#define DYN_PER_CU_PG_ENABLE                       (1 << 2)
#define RLC_SAVE_AND_RESTORE_STARTING_OFFSET 0x90
#define RLC_CLEAR_STATE_DESCRIPTOR_OFFSET    0x3D


static void gfx_v6_0_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v6_0_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v6_0_get_cu_info(struct amdgpu_device *adev);

MODULE_FIRMWARE("radeon/tahiti_pfp.bin");
MODULE_FIRMWARE("radeon/tahiti_me.bin");
MODULE_FIRMWARE("radeon/tahiti_ce.bin");
MODULE_FIRMWARE("radeon/tahiti_rlc.bin");

MODULE_FIRMWARE("radeon/pitcairn_pfp.bin");
MODULE_FIRMWARE("radeon/pitcairn_me.bin");
MODULE_FIRMWARE("radeon/pitcairn_ce.bin");
MODULE_FIRMWARE("radeon/pitcairn_rlc.bin");

MODULE_FIRMWARE("radeon/verde_pfp.bin");
MODULE_FIRMWARE("radeon/verde_me.bin");
MODULE_FIRMWARE("radeon/verde_ce.bin");
MODULE_FIRMWARE("radeon/verde_rlc.bin");

MODULE_FIRMWARE("radeon/oland_pfp.bin");
MODULE_FIRMWARE("radeon/oland_me.bin");
MODULE_FIRMWARE("radeon/oland_ce.bin");
MODULE_FIRMWARE("radeon/oland_rlc.bin");

MODULE_FIRMWARE("radeon/hainan_pfp.bin");
MODULE_FIRMWARE("radeon/hainan_me.bin");
MODULE_FIRMWARE("radeon/hainan_ce.bin");
MODULE_FIRMWARE("radeon/hainan_rlc.bin");

static u32 gfx_v6_0_get_csb_size(struct amdgpu_device *adev);
static void gfx_v6_0_get_csb_buffer(struct amdgpu_device *adev, volatile u32 *buffer);
//static void gfx_v6_0_init_cp_pg_table(struct amdgpu_device *adev);
static void gfx_v6_0_init_pg(struct amdgpu_device *adev);


static const u32 verde_rlc_save_restore_register_list[] =
{
	(0x8000 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x8000 << 16) | (0xe80 >> 2),
	0x00000000,
	(0x8040 << 16) | (0xe80 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x8040 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x8000 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x98f0 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xe7c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9148 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9148 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9150 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x897c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8d8c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac54 >> 2),
	0X00000000,
	0x3,
	(0x9c00 << 16) | (0x98f8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9910 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9914 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9918 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x991c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9920 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9924 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9928 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x992c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9930 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9934 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9938 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x993c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9940 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9944 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9948 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x994c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9950 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9954 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9958 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x995c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9960 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9964 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9968 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x996c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9970 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9974 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9978 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x997c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9980 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9984 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9988 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x998c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c00 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c04 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c08 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x8000 << 16) | (0xe84 >> 2),
	0x00000000,
	(0x8040 << 16) | (0xe84 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x914c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x914c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9354 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9354 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9060 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9364 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9100 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x913c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e0 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e4 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e0 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e4 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8bcc >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8b24 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88c4 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e50 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c0c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e58 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e5c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9508 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x950c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9494 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac0c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac10 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xae00 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac08 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88d4 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88c8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88cc >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x89b0 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8b10 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8a14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9830 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9834 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9838 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9a10 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8001 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8001 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8041 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8041 << 16) | (0x9874 >> 2),
	0x00000000,
	0x00000000
};

static int gfx_v6_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v1_0 *rlc_hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TAHITI:
		chip_name = "tahiti";
		break;
	case CHIP_PITCAIRN:
		chip_name = "pitcairn";
		break;
	case CHIP_VERDE:
		chip_name = "verde";
		break;
	case CHIP_OLAND:
		chip_name = "oland";
		break;
	case CHIP_HAINAN:
		chip_name = "hainan";
		break;
	default: BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&adev->gfx.pfp_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.pfp_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
	adev->gfx.pfp_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.pfp_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
	err = request_firmware(&adev->gfx.me_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.me_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
	adev->gfx.me_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.me_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", chip_name);
	err = request_firmware(&adev->gfx.ce_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.ce_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
	adev->gfx.ce_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.ce_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", chip_name);
	err = request_firmware(&adev->gfx.rlc_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.rlc_fw);
	rlc_hdr = (const struct rlc_firmware_header_v1_0 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_fw_version = le32_to_cpu(rlc_hdr->header.ucode_version);
	adev->gfx.rlc_feature_version = le32_to_cpu(rlc_hdr->ucode_feature_version);

out:
	if (err) {
		printk(KERN_ERR
		       "gfx6: Failed to load firmware \"%s\"\n",
		       fw_name);
		release_firmware(adev->gfx.pfp_fw);
		adev->gfx.pfp_fw = NULL;
		release_firmware(adev->gfx.me_fw);
		adev->gfx.me_fw = NULL;
		release_firmware(adev->gfx.ce_fw);
		adev->gfx.ce_fw = NULL;
		release_firmware(adev->gfx.rlc_fw);
		adev->gfx.rlc_fw = NULL;
	}
	return err;
}

static void gfx_v6_0_tiling_mode_table_init(struct amdgpu_device *adev)
{
	const u32 num_tile_mode_states = 32;
	u32 reg_offset, gb_tile_moden, split_equal_to_row_size;

	switch (adev->gfx.config.mem_row_size_in_kb) {
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

	if (adev->asic_type == CHIP_VERDE ||
		adev->asic_type == CHIP_OLAND ||
		adev->asic_type == CHIP_HAINAN) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 1: 
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 3:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 4:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 5:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 6:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 7:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 8: 
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 9:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 10:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 11:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 12:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 13:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 14:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 15:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 16:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 17:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 21:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 22:  
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 23: 
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 24: 
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 25: 
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
						 NUM_BANKS(ADDR_SURF_8_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			adev->gfx.config.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + reg_offset, gb_tile_moden);
		}
	} else if ((adev->asic_type == CHIP_TAHITI) || (adev->asic_type == CHIP_PITCAIRN)) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:  /* non-AA compressed depth or any compressed stencil */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 1:  /* 2xAA/4xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 2:  /* 8xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 3:  /* 2xAA/4xAA compressed depth with stencil (for depth buffer) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 4:  /* Maps w/ a dimension less than the 2D macro-tile dimensions (for mipmapped depth textures) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 5:  /* Uncompressed 16bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 6:  /* Uncompressed 32bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 7:  /* Uncompressed 8bpp stencil without depth (drivers typically do not use) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 8:  /* 1D and 1D Array Surfaces */
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 9:  /* Displayable maps. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 10:  /* Display 8bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 11:  /* Display 16bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 12:  /* Display 32bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 13:  /* Thin. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 14:  /* Thin 8 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 15:  /* Thin 16 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 16:  /* Thin 32 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 17:  /* Thin 64 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 21:  /* 8 bpp PRT. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 22:  /* 16 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 23:  /* 32 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 24:  /* 64 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 25:  /* 128 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
						 NUM_BANKS(ADDR_SURF_8_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			adev->gfx.config.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + reg_offset, gb_tile_moden);
		}
	} else{

		DRM_ERROR("unknown asic: 0x%x\n", adev->asic_type);
	}

}

static void gfx_v6_0_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				  u32 sh_num, u32 instance)
{
	u32 data;

	if (instance == 0xffffffff)
		data = INSTANCE_BROADCAST_WRITES;
	else
		data = INSTANCE_INDEX(instance);

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff))
		data |= SH_BROADCAST_WRITES | SE_BROADCAST_WRITES;
	else if (se_num == 0xffffffff)
		data |= SE_BROADCAST_WRITES | SH_INDEX(sh_num);
	else if (sh_num == 0xffffffff)
		data |= SH_BROADCAST_WRITES | SE_INDEX(se_num);
	else
		data |= SH_INDEX(sh_num) | SE_INDEX(se_num);
	WREG32(GRBM_GFX_INDEX, data);
}

static u32 gfx_v6_0_create_bitmask(u32 bit_width)
{
	return (u32)(((u64)1 << bit_width) - 1);
}

static u32 gfx_v6_0_get_rb_disabled(struct amdgpu_device *adev,
				    u32 max_rb_num_per_se,
				    u32 sh_per_se)
{
	u32 data, mask;

	data = RREG32(CC_RB_BACKEND_DISABLE);
	data &= BACKEND_DISABLE_MASK;
	data |= RREG32(GC_USER_RB_BACKEND_DISABLE);

	data >>= BACKEND_DISABLE_SHIFT;

	mask = gfx_v6_0_create_bitmask(max_rb_num_per_se / sh_per_se);

	return data & mask;
}

static void gfx_v6_0_raster_config(struct amdgpu_device *adev, u32 *rconf)
{
	switch (adev->asic_type) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		*rconf |= RB_XSEL2(2) | RB_XSEL | PKR_MAP(2) | PKR_YSEL(1) |
			  SE_MAP(2) | SE_XSEL(2) | SE_YSEL(2);
		break;
	case CHIP_VERDE:
		*rconf |= RB_XSEL | PKR_MAP(2) | PKR_YSEL(1);
		break;
	case CHIP_OLAND:
		*rconf |= RB_YSEL;
		break;
	case CHIP_HAINAN:
		*rconf |= 0x0;
		break;
	default:
		DRM_ERROR("unknown asic: 0x%x\n", adev->asic_type);
		break;
	}
}

static void gfx_v6_0_write_harvested_raster_configs(struct amdgpu_device *adev,
						    u32 raster_config, unsigned rb_mask,
						    unsigned num_rb)
{
	unsigned sh_per_se = max_t(unsigned, adev->gfx.config.max_sh_per_se, 1);
	unsigned num_se = max_t(unsigned, adev->gfx.config.max_shader_engines, 1);
	unsigned rb_per_pkr = min_t(unsigned, num_rb / num_se / sh_per_se, 2);
	unsigned rb_per_se = num_rb / num_se;
	unsigned se_mask[4];
	unsigned se;

	se_mask[0] = ((1 << rb_per_se) - 1) & rb_mask;
	se_mask[1] = (se_mask[0] << rb_per_se) & rb_mask;
	se_mask[2] = (se_mask[1] << rb_per_se) & rb_mask;
	se_mask[3] = (se_mask[2] << rb_per_se) & rb_mask;

	WARN_ON(!(num_se == 1 || num_se == 2 || num_se == 4));
	WARN_ON(!(sh_per_se == 1 || sh_per_se == 2));
	WARN_ON(!(rb_per_pkr == 1 || rb_per_pkr == 2));

	for (se = 0; se < num_se; se++) {
		unsigned raster_config_se = raster_config;
		unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
		unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
		int idx = (se / 2) * 2;

		if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
			raster_config_se &= ~SE_MAP_MASK;

			if (!se_mask[idx]) {
				raster_config_se |= SE_MAP(RASTER_CONFIG_SE_MAP_3);
			} else {
				raster_config_se |= SE_MAP(RASTER_CONFIG_SE_MAP_0);
			}
		}

		pkr0_mask &= rb_mask;
		pkr1_mask &= rb_mask;
		if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
			raster_config_se &= ~PKR_MAP_MASK;

			if (!pkr0_mask) {
				raster_config_se |= PKR_MAP(RASTER_CONFIG_PKR_MAP_3);
			} else {
				raster_config_se |= PKR_MAP(RASTER_CONFIG_PKR_MAP_0);
			}
		}

		if (rb_per_se >= 2) {
			unsigned rb0_mask = 1 << (se * rb_per_se);
			unsigned rb1_mask = rb0_mask << 1;

			rb0_mask &= rb_mask;
			rb1_mask &= rb_mask;
			if (!rb0_mask || !rb1_mask) {
				raster_config_se &= ~RB_MAP_PKR0_MASK;

				if (!rb0_mask) {
					raster_config_se |=
						RB_MAP_PKR0(RASTER_CONFIG_RB_MAP_3);
				} else {
					raster_config_se |=
						RB_MAP_PKR0(RASTER_CONFIG_RB_MAP_0);
				}
			}

			if (rb_per_se > 2) {
				rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
				rb1_mask = rb0_mask << 1;
				rb0_mask &= rb_mask;
				rb1_mask &= rb_mask;
				if (!rb0_mask || !rb1_mask) {
					raster_config_se &= ~RB_MAP_PKR1_MASK;

					if (!rb0_mask) {
						raster_config_se |=
							RB_MAP_PKR1(RASTER_CONFIG_RB_MAP_3);
					} else {
						raster_config_se |=
							RB_MAP_PKR1(RASTER_CONFIG_RB_MAP_0);
					}
				}
			}
		}

		/* GRBM_GFX_INDEX has a different offset on SI */
		gfx_v6_0_select_se_sh(adev, se, 0xffffffff, 0xffffffff);
		WREG32(PA_SC_RASTER_CONFIG, raster_config_se);
	}

	/* GRBM_GFX_INDEX has a different offset on SI */
	gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
}

static void gfx_v6_0_setup_rb(struct amdgpu_device *adev,
			      u32 se_num, u32 sh_per_se,
			      u32 max_rb_num_per_se)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;
	unsigned num_rb_pipes;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			gfx_v6_0_select_se_sh(adev, i, j, 0xffffffff);
			data = gfx_v6_0_get_rb_disabled(adev, max_rb_num_per_se, sh_per_se);
			disabled_rbs |= data << ((i * sh_per_se + j) * TAHITI_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = 1;
	for (i = 0; i < max_rb_num_per_se * se_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	adev->gfx.config.backend_enable_mask = enabled_rbs;
	adev->gfx.config.num_rbs = hweight32(enabled_rbs);

	num_rb_pipes = min_t(unsigned, adev->gfx.config.max_backends_per_se *
			     adev->gfx.config.max_shader_engines, 16);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < se_num; i++) {
		gfx_v6_0_select_se_sh(adev, i, 0xffffffff, 0xffffffff);
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
		gfx_v6_0_raster_config(adev, &data);

		if (!adev->gfx.config.backend_enable_mask ||
				adev->gfx.config.num_rbs >= num_rb_pipes)
			WREG32(PA_SC_RASTER_CONFIG, data);
		else
			gfx_v6_0_write_harvested_raster_configs(adev, data,
								adev->gfx.config.backend_enable_mask,
								num_rb_pipes);
	}
	gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}
/*
static void gmc_v6_0_init_compute_vmid(struct amdgpu_device *adev)
{
}
*/

static u32 gfx_v6_0_get_cu_enabled(struct amdgpu_device *adev, u32 cu_per_sh)
{
	u32 data, mask;

	data = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	data &= INACTIVE_CUS_MASK;
	data |= RREG32(GC_USER_SHADER_ARRAY_CONFIG);

	data >>= INACTIVE_CUS_SHIFT;

	mask = gfx_v6_0_create_bitmask(cu_per_sh);

	return ~data & mask;
}


static void gfx_v6_0_setup_spi(struct amdgpu_device *adev,
			 u32 se_num, u32 sh_per_se,
			 u32 cu_per_sh)
{
	int i, j, k;
	u32 data, mask;
	u32 active_cu = 0;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			gfx_v6_0_select_se_sh(adev, i, j, 0xffffffff);
			data = RREG32(SPI_STATIC_THREAD_MGMT_3);
			active_cu = gfx_v6_0_get_cu_enabled(adev, cu_per_sh);

			mask = 1;
			for (k = 0; k < 16; k++) {
				mask <<= k;
				if (active_cu & mask) {
					data &= ~mask;
					WREG32(SPI_STATIC_THREAD_MGMT_3, data);
					break;
				}
			}
		}
	}
	gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v6_0_gpu_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config = 0;
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 sx_debug_1;
	u32 hdp_host_path_cntl;
	u32 tmp;

	switch (adev->asic_type) {
	case CHIP_TAHITI:
		adev->gfx.config.max_shader_engines = 2;
		adev->gfx.config.max_tile_pipes = 12;
		adev->gfx.config.max_cu_per_sh = 8;
		adev->gfx.config.max_sh_per_se = 2;
		adev->gfx.config.max_backends_per_se = 4;
		adev->gfx.config.max_texture_channel_caches = 12;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_PITCAIRN:
		adev->gfx.config.max_shader_engines = 2;
		adev->gfx.config.max_tile_pipes = 8;
		adev->gfx.config.max_cu_per_sh = 5;
		adev->gfx.config.max_sh_per_se = 2;
		adev->gfx.config.max_backends_per_se = 4;
		adev->gfx.config.max_texture_channel_caches = 8;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;

	case CHIP_VERDE:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 4;
		adev->gfx.config.max_cu_per_sh = 5;
		adev->gfx.config.max_sh_per_se = 2;
		adev->gfx.config.max_backends_per_se = 4;
		adev->gfx.config.max_texture_channel_caches = 4;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x40;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_OLAND:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 4;
		adev->gfx.config.max_cu_per_sh = 6;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 4;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 16;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x40;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_HAINAN:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 4;
		adev->gfx.config.max_cu_per_sh = 5;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 1;
		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 16;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x40;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = HAINAN_GB_ADDR_CONFIG_GOLDEN;
		break;
	default:
		BUG();
		break;
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));
	WREG32(SRBM_INT_CNTL, 1);
	WREG32(SRBM_INT_ACK, 1);

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	adev->gfx.config.num_tile_pipes = adev->gfx.config.max_tile_pipes;
	adev->gfx.config.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	adev->gfx.config.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (adev->gfx.config.mem_row_size_in_kb > 4)
		adev->gfx.config.mem_row_size_in_kb = 4;
	adev->gfx.config.shader_engine_tile_size = 32;
	adev->gfx.config.num_gpus = 1;
	adev->gfx.config.multi_gpu_tile_size = 64;

	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (adev->gfx.config.mem_row_size_in_kb) {
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
	adev->gfx.config.gb_addr_config = gb_addr_config;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA0_REGISTER_OFFSET, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA1_REGISTER_OFFSET, gb_addr_config);
#if 0
	if (adev->has_uvd) {
		WREG32(UVD_UDEC_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);
	}
#endif
	gfx_v6_0_tiling_mode_table_init(adev);

	gfx_v6_0_setup_rb(adev, adev->gfx.config.max_shader_engines,
		    adev->gfx.config.max_sh_per_se,
		    adev->gfx.config.max_backends_per_se);

	gfx_v6_0_setup_spi(adev, adev->gfx.config.max_shader_engines,
		     adev->gfx.config.max_sh_per_se,
		     adev->gfx.config.max_cu_per_sh);

	gfx_v6_0_get_cu_info(adev);

	WREG32(CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) |
				     ROQ_IB2_START(0x2b)));
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	sx_debug_1 = RREG32(SX_DEBUG_1);
	WREG32(SX_DEBUG_1, sx_debug_1);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(adev->gfx.config.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(adev->gfx.config.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(adev->gfx.config.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(adev->gfx.config.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);
	WREG32(CP_PERFMON_CNTL, 0);
	WREG32(SQ_CONFIG, 0);
	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	WREG32(CB_PERFCOUNTER0_SELECT0, 0);
	WREG32(CB_PERFCOUNTER0_SELECT1, 0);
	WREG32(CB_PERFCOUNTER1_SELECT0, 0);
	WREG32(CB_PERFCOUNTER1_SELECT1, 0);
	WREG32(CB_PERFCOUNTER2_SELECT0, 0);
	WREG32(CB_PERFCOUNTER2_SELECT1, 0);
	WREG32(CB_PERFCOUNTER3_SELECT0, 0);
	WREG32(CB_PERFCOUNTER3_SELECT1, 0);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	udelay(50);
}


static void gfx_v6_0_scratch_init(struct amdgpu_device *adev)
{
	int i;

	adev->gfx.scratch.num_reg = 7;
	adev->gfx.scratch.reg_base = SCRATCH_REG0;
	for (i = 0; i < adev->gfx.scratch.num_reg; i++) {
		adev->gfx.scratch.free[i] = true;
		adev->gfx.scratch.reg[i] = adev->gfx.scratch.reg_base + i;
	}
}

static int gfx_v6_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);

	r = amdgpu_ring_alloc(ring, 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n", ring->idx, r);
		amdgpu_gfx_scratch_free(adev, scratch);
		return r;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	amdgpu_ring_write(ring, (scratch - PACKET3_SET_CONFIG_REG_START));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (scratch(0x%04X)=0x%08X)\n",
			  ring->idx, scratch, tmp);
		r = -EINVAL;
	}
	amdgpu_gfx_scratch_free(adev, scratch);
	return r;
}

static void gfx_v6_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	/* flush hdp cache */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, HDP_MEM_COHERENCY_FLUSH_CNTL);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0x1);
}

/**
 * gfx_v6_0_ring_emit_hdp_invalidate - emit an hdp invalidate on the cp
 *
 * @adev: amdgpu_device pointer
 * @ridx: amdgpu ring index
 *
 * Emits an hdp invalidate on the cp.
 */
static void gfx_v6_0_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, HDP_DEBUG0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0x1);
}

static void gfx_v6_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				     u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;
	/* flush read cache over gart */
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	amdgpu_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	amdgpu_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
			  PACKET3_TC_ACTION_ENA |
			  PACKET3_SH_KCACHE_ACTION_ENA |
			  PACKET3_SH_ICACHE_ACTION_ENA);
	amdgpu_ring_write(ring, 0xFFFFFFFF);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 10); /* poll interval */
	/* EVENT_WRITE_EOP - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	amdgpu_ring_write(ring, EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xffff) |
				DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
}

static void gfx_v6_0_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vm_id, bool ctx_switch)
{
	u32 header, control = 0;

	/* insert SWITCH_BUFFER packet before first IB in the ring frame */
	if (ctx_switch) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}

	if (ib->flags & AMDGPU_IB_FLAG_CE)
		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	else
		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vm_id << 24);

	amdgpu_ring_write(ring, header);
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	amdgpu_ring_write(ring, control);
}

/**
 * gfx_v6_0_ring_test_ib - basic ring IB test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Allocate an IB and execute it on the gfx ring (SI).
 * Provides a basic gfx ring test to verify that IBs are working.
 * Returns 0 on success, error on failure.
 */
static int gfx_v6_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	uint32_t scratch;
	uint32_t tmp = 0;
	long r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: failed to get scratch reg (%ld).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
		goto err1;
	}
	ib.ptr[0] = PACKET3(PACKET3_SET_CONFIG_REG, 1);
	ib.ptr[1] = ((scratch - PACKET3_SET_CONFIG_REG_START));
	ib.ptr[2] = 0xDEADBEEF;
	ib.length_dw = 3;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, NULL, &f);
	if (r)
		goto err2;

	r = fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out\n");
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto err2;
	}
	tmp = RREG32(scratch);
	if (tmp == 0xDEADBEEF) {
		DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	} else {
		DRM_ERROR("amdgpu: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}

err2:
	amdgpu_ib_free(adev, &ib, NULL);
	fence_put(f);
err1:
	amdgpu_gfx_scratch_free(adev, scratch);
	return r;
}

static void gfx_v6_0_cp_gfx_enable(struct amdgpu_device *adev, bool enable)
{
	int i;
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT));
		WREG32(SCRATCH_UMSK, 0);
		for (i = 0; i < adev->gfx.num_gfx_rings; i++)
			adev->gfx.gfx_ring[i].ready = false;
		for (i = 0; i < adev->gfx.num_compute_rings; i++)
			adev->gfx.compute_ring[i].ready = false;
	}
	udelay(50);
}

static int gfx_v6_0_cp_gfx_load_microcode(struct amdgpu_device *adev)
{
	unsigned i;
	const struct gfx_firmware_header_v1_0 *pfp_hdr;
	const struct gfx_firmware_header_v1_0 *ce_hdr;
	const struct gfx_firmware_header_v1_0 *me_hdr;
	const __le32 *fw_data;
	u32 fw_size;

	if (!adev->gfx.me_fw || !adev->gfx.pfp_fw || !adev->gfx.ce_fw)
		return -EINVAL;

	gfx_v6_0_cp_gfx_enable(adev, false);
	pfp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
	ce_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
	me_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;

	amdgpu_ucode_print_gfx_hdr(&pfp_hdr->header);
	amdgpu_ucode_print_gfx_hdr(&ce_hdr->header);
	amdgpu_ucode_print_gfx_hdr(&me_hdr->header);

	/* PFP */
	fw_data = (const __le32 *)
		(adev->gfx.pfp_fw->data + le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes) / 4;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(CP_PFP_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	/* CE */
	fw_data = (const __le32 *)
		(adev->gfx.ce_fw->data + le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes) / 4;
	WREG32(CP_CE_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(CP_CE_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32(CP_CE_UCODE_ADDR, 0);

	/* ME */
	fw_data = (const __be32 *)
		(adev->gfx.me_fw->data + le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes) / 4;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(CP_ME_RAM_DATA, le32_to_cpup(fw_data++));
	WREG32(CP_ME_RAM_WADDR, 0);


	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_CE_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

static int gfx_v6_0_cp_gfx_start(struct amdgpu_device *adev)
{
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	struct amdgpu_ring *ring = &adev->gfx.gfx_ring[0];
	int r, i;

	r = amdgpu_ring_alloc(ring, 7 + 4);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_ME_INITIALIZE, 5));
	amdgpu_ring_write(ring, 0x1);
	amdgpu_ring_write(ring, 0x0);
	amdgpu_ring_write(ring, adev->gfx.config.max_hw_contexts - 1);
	amdgpu_ring_write(ring, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	amdgpu_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	amdgpu_ring_write(ring, 0xc000);
	amdgpu_ring_write(ring, 0xe000);
	amdgpu_ring_commit(ring);

	gfx_v6_0_cp_gfx_enable(adev, true);

	r = amdgpu_ring_alloc(ring, gfx_v6_0_get_csb_size(adev) + 10);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				amdgpu_ring_write(ring,
						  PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				amdgpu_ring_write(ring, ext->reg_index - PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					amdgpu_ring_write(ring, ext->extent[i]);
			}
		}
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	amdgpu_ring_write(ring, 0x00000316);
	amdgpu_ring_write(ring, 0x0000000e);
	amdgpu_ring_write(ring, 0x00000010);

	amdgpu_ring_commit(ring);

	return 0;
}

static int gfx_v6_0_cp_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	int r;
	u64 rptr_addr;

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	WREG32(CP_DEBUG, 0);
	WREG32(SCRATCH_ADDR, 0);

	/* ring 0 - compute and gfx */
	/* Set ring buffer size */
	ring = &adev->gfx.gfx_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(AMDGPU_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;

#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB0_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB0_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32(CP_RB0_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32(CP_RB0_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & 0xFF);

	WREG32(SCRATCH_UMSK, 0);

	mdelay(1);
	WREG32(CP_RB0_CNTL, tmp);

	WREG32(CP_RB0_BASE, ring->gpu_addr >> 8);

	/* start the rings */
	gfx_v6_0_cp_gfx_start(adev);
	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		return r;
	}

	return 0;
}

static u32 gfx_v6_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs];
}

static u32 gfx_v6_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->gfx.gfx_ring[0])
		return RREG32(CP_RB0_WPTR);
	else if (ring == &adev->gfx.compute_ring[0])
		return RREG32(CP_RB1_WPTR);
	else if (ring == &adev->gfx.compute_ring[1])
		return RREG32(CP_RB2_WPTR);
	else
		BUG();
}

static void gfx_v6_0_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32(CP_RB0_WPTR, ring->wptr);
	(void)RREG32(CP_RB0_WPTR);
}

static void gfx_v6_0_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->gfx.compute_ring[0]) {
		WREG32(CP_RB1_WPTR, ring->wptr);
		(void)RREG32(CP_RB1_WPTR);
	} else if (ring == &adev->gfx.compute_ring[1]) {
		WREG32(CP_RB2_WPTR, ring->wptr);
		(void)RREG32(CP_RB2_WPTR);
	} else {
		BUG();
	}

}

static int gfx_v6_0_cp_compute_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	int r;
	u64 rptr_addr;

	/* ring1  - compute only */
	/* Set ring buffer size */

	ring = &adev->gfx.compute_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(AMDGPU_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB1_CNTL, tmp);

	WREG32(CP_RB1_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB1_WPTR, ring->wptr);

	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32(CP_RB1_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32(CP_RB1_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & 0xFF);

	mdelay(1);
	WREG32(CP_RB1_CNTL, tmp);
	WREG32(CP_RB1_BASE, ring->gpu_addr >> 8);

	ring = &adev->gfx.compute_ring[1];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(AMDGPU_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB2_CNTL, tmp);

	WREG32(CP_RB2_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB2_WPTR, ring->wptr);
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32(CP_RB2_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32(CP_RB2_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & 0xFF);

	mdelay(1);
	WREG32(CP_RB2_CNTL, tmp);
	WREG32(CP_RB2_BASE, ring->gpu_addr >> 8);

	adev->gfx.compute_ring[0].ready = true;
	adev->gfx.compute_ring[1].ready = true;

	r = amdgpu_ring_test_ring(&adev->gfx.compute_ring[0]);
	if (r) {
		adev->gfx.compute_ring[0].ready = false;
		return r;
	}

	r = amdgpu_ring_test_ring(&adev->gfx.compute_ring[1]);
	if (r) {
		adev->gfx.compute_ring[1].ready = false;
		return r;
	}

	return 0;
}

static void gfx_v6_0_cp_enable(struct amdgpu_device *adev, bool enable)
{
	gfx_v6_0_cp_gfx_enable(adev, enable);
}

static int gfx_v6_0_cp_load_microcode(struct amdgpu_device *adev)
{
	return gfx_v6_0_cp_gfx_load_microcode(adev);
}

static void gfx_v6_0_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{	
	u32 tmp = RREG32(CP_INT_CNTL_RING0);
	u32 mask;
	int i;

	if (enable)
		tmp |= (CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	else
		tmp &= ~(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);

	if (!enable) {
		/* read a gfx register */
		tmp = RREG32(DB_DEPTH_INFO);

		mask = RLC_BUSY_STATUS | GFX_POWER_STATUS | GFX_CLOCK_STATUS | GFX_LS_STATUS;
		for (i = 0; i < adev->usec_timeout; i++) {
			if ((RREG32(RLC_STAT) & mask) == (GFX_CLOCK_STATUS | GFX_POWER_STATUS))
				break;
			udelay(1);
		}
	}
}

static int gfx_v6_0_cp_resume(struct amdgpu_device *adev)
{
	int r;

	gfx_v6_0_enable_gui_idle_interrupt(adev, false);

	r = gfx_v6_0_cp_load_microcode(adev);
	if (r)
		return r;

	r = gfx_v6_0_cp_gfx_resume(adev);
	if (r)
		return r;
	r = gfx_v6_0_cp_compute_resume(adev);
	if (r)
		return r;

	gfx_v6_0_enable_gui_idle_interrupt(adev, true);

	return 0;
}

static void gfx_v6_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_MEM_SPACE(1) | /* memory */
				 WAIT_REG_MEM_FUNCTION(3) | /* equal */
				 WAIT_REG_MEM_ENGINE(usepfp)));   /* pfp or me */
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, 0xffffffff);
	amdgpu_ring_write(ring, 4); /* poll interval */

	if (usepfp) {
		/* synce CE with ME to prevent CE fetch CEIB before context switch done */
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}
}

static void gfx_v6_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vm_id, uint64_t pd_addr)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);

	/* write new base address */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	if (vm_id < 8) {
		amdgpu_ring_write(ring, (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id ));
	} else {
		amdgpu_ring_write(ring, (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + (vm_id - 8)));
	}
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, pd_addr >> 12);

	/* bits 0-15 are the VM contexts0-15 */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, VM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 1 << vm_id);

	/* wait for the invalidate to complete */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	amdgpu_ring_write(ring, VM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* ref */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, 0x20); /* poll interval */

	if (usepfp) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);

		/* synce CE with ME to prevent CE fetch CEIB before context switch done */
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}
}


static void gfx_v6_0_rlc_fini(struct amdgpu_device *adev)
{
	int r;

	if (adev->gfx.rlc.save_restore_obj) {
		r = amdgpu_bo_reserve(adev->gfx.rlc.save_restore_obj, false);
		if (unlikely(r != 0))
			dev_warn(adev->dev, "(%d) reserve RLC sr bo failed\n", r);
		amdgpu_bo_unpin(adev->gfx.rlc.save_restore_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.save_restore_obj);

		amdgpu_bo_unref(&adev->gfx.rlc.save_restore_obj);
		adev->gfx.rlc.save_restore_obj = NULL;
	}

	if (adev->gfx.rlc.clear_state_obj) {
		r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
		if (unlikely(r != 0))
			dev_warn(adev->dev, "(%d) reserve RLC c bo failed\n", r);
		amdgpu_bo_unpin(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);

		amdgpu_bo_unref(&adev->gfx.rlc.clear_state_obj);
		adev->gfx.rlc.clear_state_obj = NULL;
	}

	if (adev->gfx.rlc.cp_table_obj) {
		r = amdgpu_bo_reserve(adev->gfx.rlc.cp_table_obj, false);
		if (unlikely(r != 0))
			dev_warn(adev->dev, "(%d) reserve RLC cp table bo failed\n", r);
		amdgpu_bo_unpin(adev->gfx.rlc.cp_table_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.cp_table_obj);

		amdgpu_bo_unref(&adev->gfx.rlc.cp_table_obj);
		adev->gfx.rlc.cp_table_obj = NULL;
	}
}

static int gfx_v6_0_rlc_init(struct amdgpu_device *adev)
{
	const u32 *src_ptr;
	volatile u32 *dst_ptr;
	u32 dws, i;
	u64 reg_list_mc_addr;
	const struct cs_section_def *cs_data;
	int r;

	adev->gfx.rlc.reg_list = verde_rlc_save_restore_register_list;
	adev->gfx.rlc.reg_list_size =
			(u32)ARRAY_SIZE(verde_rlc_save_restore_register_list);

	adev->gfx.rlc.cs_data = si_cs_data;
	src_ptr = adev->gfx.rlc.reg_list;
	dws = adev->gfx.rlc.reg_list_size;
	cs_data = adev->gfx.rlc.cs_data;

	if (src_ptr) {
		/* save restore block */
		if (adev->gfx.rlc.save_restore_obj == NULL) {

			r = amdgpu_bo_create(adev, dws * 4, PAGE_SIZE, true,
					     AMDGPU_GEM_DOMAIN_VRAM,
					     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
					     NULL, NULL,
					     &adev->gfx.rlc.save_restore_obj);

			if (r) {
				dev_warn(adev->dev, "(%d) create RLC sr bo failed\n", r);
				return r;
			}
		}

		r = amdgpu_bo_reserve(adev->gfx.rlc.save_restore_obj, false);
		if (unlikely(r != 0)) {
			gfx_v6_0_rlc_fini(adev);
			return r;
		}
		r = amdgpu_bo_pin(adev->gfx.rlc.save_restore_obj, AMDGPU_GEM_DOMAIN_VRAM,
				  &adev->gfx.rlc.save_restore_gpu_addr);
		if (r) {
			amdgpu_bo_unreserve(adev->gfx.rlc.save_restore_obj);
			dev_warn(adev->dev, "(%d) pin RLC sr bo failed\n", r);
			gfx_v6_0_rlc_fini(adev);
			return r;
		}

		r = amdgpu_bo_kmap(adev->gfx.rlc.save_restore_obj, (void **)&adev->gfx.rlc.sr_ptr);
		if (r) {
			dev_warn(adev->dev, "(%d) map RLC sr bo failed\n", r);
			gfx_v6_0_rlc_fini(adev);
			return r;
		}
		/* write the sr buffer */
		dst_ptr = adev->gfx.rlc.sr_ptr;
		for (i = 0; i < adev->gfx.rlc.reg_list_size; i++)
			dst_ptr[i] = cpu_to_le32(src_ptr[i]);
		amdgpu_bo_kunmap(adev->gfx.rlc.save_restore_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.save_restore_obj);
	}

	if (cs_data) {
		/* clear state block */
		adev->gfx.rlc.clear_state_size = gfx_v6_0_get_csb_size(adev);
		dws = adev->gfx.rlc.clear_state_size + (256 / 4);

		if (adev->gfx.rlc.clear_state_obj == NULL) {
			r = amdgpu_bo_create(adev, dws * 4, PAGE_SIZE, true,
					     AMDGPU_GEM_DOMAIN_VRAM,
					     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
					     NULL, NULL,
					     &adev->gfx.rlc.clear_state_obj);

			if (r) {
				dev_warn(adev->dev, "(%d) create RLC c bo failed\n", r);
				gfx_v6_0_rlc_fini(adev);
				return r;
			}
		}
		r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
		if (unlikely(r != 0)) {
			gfx_v6_0_rlc_fini(adev);
			return r;
		}
		r = amdgpu_bo_pin(adev->gfx.rlc.clear_state_obj, AMDGPU_GEM_DOMAIN_VRAM,
				  &adev->gfx.rlc.clear_state_gpu_addr);
		if (r) {
			amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
			dev_warn(adev->dev, "(%d) pin RLC c bo failed\n", r);
			gfx_v6_0_rlc_fini(adev);
			return r;
		}

		r = amdgpu_bo_kmap(adev->gfx.rlc.clear_state_obj, (void **)&adev->gfx.rlc.cs_ptr);
		if (r) {
			dev_warn(adev->dev, "(%d) map RLC c bo failed\n", r);
			gfx_v6_0_rlc_fini(adev);
			return r;
		}
		/* set up the cs buffer */
		dst_ptr = adev->gfx.rlc.cs_ptr;
		reg_list_mc_addr = adev->gfx.rlc.clear_state_gpu_addr + 256;
		dst_ptr[0] = cpu_to_le32(upper_32_bits(reg_list_mc_addr));
		dst_ptr[1] = cpu_to_le32(lower_32_bits(reg_list_mc_addr));
		dst_ptr[2] = cpu_to_le32(adev->gfx.rlc.clear_state_size);
		gfx_v6_0_get_csb_buffer(adev, &dst_ptr[(256/4)]);
		amdgpu_bo_kunmap(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
	}

	return 0;
}

static void gfx_v6_0_enable_lbpw(struct amdgpu_device *adev, bool enable)
{
	u32 tmp;

	tmp = RREG32(RLC_LB_CNTL);
	if (enable)
		tmp |= LOAD_BALANCE_ENABLE;
	else
		tmp &= ~LOAD_BALANCE_ENABLE;
	WREG32(RLC_LB_CNTL, tmp);

	if (!enable) {
		gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
		WREG32(SPI_LB_CU_MASK, 0x00ff);
	}

}

static void gfx_v6_0_wait_for_rlc_serdes(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(RLC_SERDES_MASTER_BUSY_0) == 0)
			break;
		udelay(1);
	}

	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(RLC_SERDES_MASTER_BUSY_1) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v6_0_update_rlc(struct amdgpu_device *adev, u32 rlc)
{
	u32 tmp;

	tmp = RREG32(RLC_CNTL);
	if (tmp != rlc)
		WREG32(RLC_CNTL, rlc);
}

static u32 gfx_v6_0_halt_rlc(struct amdgpu_device *adev)
{
	u32 data, orig;

	orig = data = RREG32(RLC_CNTL);

	if (data & RLC_ENABLE) {
		data &= ~RLC_ENABLE;
		WREG32(RLC_CNTL, data);

		gfx_v6_0_wait_for_rlc_serdes(adev);
	}

	return orig;
}

static void gfx_v6_0_rlc_stop(struct amdgpu_device *adev)
{
	WREG32(RLC_CNTL, 0);

	gfx_v6_0_enable_gui_idle_interrupt(adev, false);
	gfx_v6_0_wait_for_rlc_serdes(adev);
}

static void gfx_v6_0_rlc_start(struct amdgpu_device *adev)
{
	WREG32(RLC_CNTL, RLC_ENABLE);

	gfx_v6_0_enable_gui_idle_interrupt(adev, true);

	udelay(50);
}

static void gfx_v6_0_rlc_reset(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(GRBM_SOFT_RESET);

	tmp |= SOFT_RESET_RLC;
	WREG32(GRBM_SOFT_RESET, tmp);
	udelay(50);
	tmp &= ~SOFT_RESET_RLC;
	WREG32(GRBM_SOFT_RESET, tmp);
	udelay(50);
}

static bool gfx_v6_0_lbpw_supported(struct amdgpu_device *adev)
{
	u32 tmp;

	/* Enable LBPW only for DDR3 */
	tmp = RREG32(MC_SEQ_MISC0);
	if ((tmp & 0xF0000000) == 0xB0000000)
		return true;
	return false;
}
static void gfx_v6_0_init_cg(struct amdgpu_device *adev)
{
}

static int gfx_v6_0_rlc_resume(struct amdgpu_device *adev)
{
	u32 i;
	const struct rlc_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;


	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	gfx_v6_0_rlc_stop(adev);
	gfx_v6_0_rlc_reset(adev);
	gfx_v6_0_init_pg(adev);
	gfx_v6_0_init_cg(adev);

	WREG32(RLC_RL_BASE, 0);
	WREG32(RLC_RL_SIZE, 0);
	WREG32(RLC_LB_CNTL, 0);
	WREG32(RLC_LB_CNTR_MAX, 0xffffffff);
	WREG32(RLC_LB_CNTR_INIT, 0);
	WREG32(RLC_LB_INIT_CU_MASK, 0xffffffff);

	WREG32(RLC_MC_CNTL, 0);
	WREG32(RLC_UCODE_CNTL, 0);

	hdr = (const struct rlc_firmware_header_v1_0 *)adev->gfx.rlc_fw->data;
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
	fw_data = (const __le32 *)
		(adev->gfx.rlc_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	for (i = 0; i < fw_size; i++) {
		WREG32(RLC_UCODE_ADDR, i);
		WREG32(RLC_UCODE_DATA, le32_to_cpup(fw_data++));
	}
	WREG32(RLC_UCODE_ADDR, 0);

	gfx_v6_0_enable_lbpw(adev, gfx_v6_0_lbpw_supported(adev));
	gfx_v6_0_rlc_start(adev);

	return 0;
}

static void gfx_v6_0_enable_cgcg(struct amdgpu_device *adev, bool enable)
{
	u32 data, orig, tmp;

	orig = data = RREG32(RLC_CGCG_CGLS_CTRL);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {
		gfx_v6_0_enable_gui_idle_interrupt(adev, true);

		WREG32(RLC_GCPM_GENERAL_3, 0x00000080);

		tmp = gfx_v6_0_halt_rlc(adev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00b000ff);

		gfx_v6_0_wait_for_rlc_serdes(adev);
		gfx_v6_0_update_rlc(adev, tmp);

		WREG32(RLC_SERDES_WR_CTRL, 0x007000ff);

		data |= CGCG_EN | CGLS_EN;
	} else {
		gfx_v6_0_enable_gui_idle_interrupt(adev, false);

		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);

		data &= ~(CGCG_EN | CGLS_EN);
	}

	if (orig != data)
		WREG32(RLC_CGCG_CGLS_CTRL, data);

}

static void gfx_v6_0_enable_mgcg(struct amdgpu_device *adev, bool enable)
{

	u32 data, orig, tmp = 0;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		orig = data = RREG32(CGTS_SM_CTRL_REG);
		data = 0x96940200;
		if (orig != data)
			WREG32(CGTS_SM_CTRL_REG, data);

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
			orig = data = RREG32(CP_MEM_SLP_CNTL);
			data |= CP_MEM_LS_EN;
			if (orig != data)
				WREG32(CP_MEM_SLP_CNTL, data);
		}

		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data &= 0xffffffc0;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		tmp = gfx_v6_0_halt_rlc(adev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00d000ff);

		gfx_v6_0_update_rlc(adev, tmp);
	} else {
		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data |= 0x00000003;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		data = RREG32(CP_MEM_SLP_CNTL);
		if (data & CP_MEM_LS_EN) {
			data &= ~CP_MEM_LS_EN;
			WREG32(CP_MEM_SLP_CNTL, data);
		}
		orig = data = RREG32(CGTS_SM_CTRL_REG);
		data |= LS_OVERRIDE | OVERRIDE;
		if (orig != data)
			WREG32(CGTS_SM_CTRL_REG, data);

		tmp = gfx_v6_0_halt_rlc(adev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00e000ff);

		gfx_v6_0_update_rlc(adev, tmp);
	}
}
/*
static void gfx_v6_0_update_cg(struct amdgpu_device *adev,
			       bool enable)
{
	gfx_v6_0_enable_gui_idle_interrupt(adev, false);
	if (enable) {
		gfx_v6_0_enable_mgcg(adev, true);
		gfx_v6_0_enable_cgcg(adev, true);
	} else {
		gfx_v6_0_enable_cgcg(adev, false);
		gfx_v6_0_enable_mgcg(adev, false);
	}
	gfx_v6_0_enable_gui_idle_interrupt(adev, true);
}
*/
static void gfx_v6_0_enable_sclk_slowdown_on_pu(struct amdgpu_device *adev,
						bool enable)
{
}

static void gfx_v6_0_enable_sclk_slowdown_on_pd(struct amdgpu_device *adev,
						bool enable)
{
}

static void gfx_v6_0_enable_cp_pg(struct amdgpu_device *adev, bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_CP))
		data &= ~0x8000;
	else
		data |= 0x8000;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void gfx_v6_0_enable_gds_pg(struct amdgpu_device *adev, bool enable)
{
}
/*
static void gfx_v6_0_init_cp_pg_table(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	volatile u32 *dst_ptr;
	int me, i, max_me = 4;
	u32 bo_offset = 0;
	u32 table_offset, table_size;

	if (adev->asic_type == CHIP_KAVERI)
		max_me = 5;

	if (adev->gfx.rlc.cp_table_ptr == NULL)
		return;

	dst_ptr = adev->gfx.rlc.cp_table_ptr;
	for (me = 0; me < max_me; me++) {
		if (me == 0) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.ce_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 1) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.pfp_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 2) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.me_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 3) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.mec_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.mec2_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.mec2_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		}

		for (i = 0; i < table_size; i ++) {
			dst_ptr[bo_offset + i] =
				cpu_to_le32(le32_to_cpu(fw_data[table_offset + i]));
		}

		bo_offset += table_size;
	}
}
*/
static void gfx_v6_0_enable_gfx_cgpg(struct amdgpu_device *adev,
				     bool enable)
{

	u32 tmp;

	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_GFX_PG)) {
		tmp = RLC_PUD(0x10) | RLC_PDD(0x10) | RLC_TTPD(0x10) | RLC_MSD(0x10);
		WREG32(RLC_TTOP_D, tmp);

		tmp = RREG32(RLC_PG_CNTL);
		tmp |= GFX_PG_ENABLE;
		WREG32(RLC_PG_CNTL, tmp);

		tmp = RREG32(RLC_AUTO_PG_CTRL);
		tmp |= AUTO_PG_EN;
		WREG32(RLC_AUTO_PG_CTRL, tmp);
	} else {
		tmp = RREG32(RLC_AUTO_PG_CTRL);
		tmp &= ~AUTO_PG_EN;
		WREG32(RLC_AUTO_PG_CTRL, tmp);

		tmp = RREG32(DB_RENDER_CONTROL);
	}
}

static u32 gfx_v6_0_get_cu_active_bitmap(struct amdgpu_device *adev,
					 u32 se, u32 sh)
{

	u32 mask = 0, tmp, tmp1;
	int i;

	mutex_lock(&adev->grbm_idx_mutex);
	gfx_v6_0_select_se_sh(adev, se, sh, 0xffffffff);
	tmp = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	tmp1 = RREG32(GC_USER_SHADER_ARRAY_CONFIG);
	gfx_v6_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	tmp &= 0xffff0000;

	tmp |= tmp1;
	tmp >>= 16;

	for (i = 0; i < adev->gfx.config.max_cu_per_sh; i ++) {
		mask <<= 1;
		mask |= 1;
	}

	return (~tmp) & mask;
}

static void gfx_v6_0_init_ao_cu_mask(struct amdgpu_device *adev)
{
	u32 i, j, k, active_cu_number = 0;

	u32 mask, counter, cu_bitmap;
	u32 tmp = 0;

	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			cu_bitmap = 0;
			counter  = 0;
			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k++) {
				if (gfx_v6_0_get_cu_active_bitmap(adev, i, j) & mask) {
					if (counter < 2)
						cu_bitmap |= mask;
					counter++;
				}
				mask <<= 1;
			}

			active_cu_number += counter;
			tmp |= (cu_bitmap << (i * 16 + j * 8));
		}
	}

	WREG32(RLC_PG_AO_CU_MASK, tmp);

	tmp = RREG32(RLC_MAX_PG_CU);
	tmp &= ~MAX_PU_CU_MASK;
	tmp |= MAX_PU_CU(active_cu_number);
	WREG32(RLC_MAX_PG_CU, tmp);
}

static void gfx_v6_0_enable_gfx_static_mgpg(struct amdgpu_device *adev,
					    bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_GFX_SMG))
		data |= STATIC_PER_CU_PG_ENABLE;
	else
		data &= ~STATIC_PER_CU_PG_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void gfx_v6_0_enable_gfx_dynamic_mgpg(struct amdgpu_device *adev,
					     bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_GFX_DMG))
		data |= DYN_PER_CU_PG_ENABLE;
	else
		data &= ~DYN_PER_CU_PG_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void gfx_v6_0_init_gfx_cgpg(struct amdgpu_device *adev)
{
	u32 tmp;

	WREG32(RLC_SAVE_AND_RESTORE_BASE, adev->gfx.rlc.save_restore_gpu_addr >> 8);

	tmp = RREG32(RLC_PG_CNTL);
	tmp |= GFX_PG_SRC;
	WREG32(RLC_PG_CNTL, tmp);

	WREG32(RLC_CLEAR_STATE_RESTORE_BASE, adev->gfx.rlc.clear_state_gpu_addr >> 8);

	tmp = RREG32(RLC_AUTO_PG_CTRL);

	tmp &= ~GRBM_REG_SGIT_MASK;
	tmp |= GRBM_REG_SGIT(0x700);
	tmp &= ~PG_AFTER_GRBM_REG_ST_MASK;
	WREG32(RLC_AUTO_PG_CTRL, tmp);
}

static void gfx_v6_0_update_gfx_pg(struct amdgpu_device *adev, bool enable)
{
	gfx_v6_0_enable_gfx_cgpg(adev, enable);
	gfx_v6_0_enable_gfx_static_mgpg(adev, enable);
	gfx_v6_0_enable_gfx_dynamic_mgpg(adev, enable);
}

static u32 gfx_v6_0_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (adev->gfx.rlc.cs_data == NULL)
		return 0;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}
	/* pa_sc_raster_config */
	count += 3;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

static void gfx_v6_0_get_csb_buffer(struct amdgpu_device *adev,
				    volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (adev->gfx.rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index - 0xa000);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	buffer[count++] = cpu_to_le32(PA_SC_RASTER_CONFIG - PACKET3_SET_CONTEXT_REG_START);

	switch (adev->asic_type) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		buffer[count++] = cpu_to_le32(0x2a00126a);
		break;
	case CHIP_VERDE:
		buffer[count++] = cpu_to_le32(0x0000124a);
		break;
	case CHIP_OLAND:
		buffer[count++] = cpu_to_le32(0x00000082);
		break;
	case CHIP_HAINAN:
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	default:
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void gfx_v6_0_init_pg(struct amdgpu_device *adev)
{
	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG |
			      AMD_PG_SUPPORT_CP |
			      AMD_PG_SUPPORT_GDS |
			      AMD_PG_SUPPORT_RLC_SMU_HS)) {
		gfx_v6_0_enable_sclk_slowdown_on_pu(adev, true);
		gfx_v6_0_enable_sclk_slowdown_on_pd(adev, true);
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) {
			gfx_v6_0_init_gfx_cgpg(adev);
			gfx_v6_0_enable_cp_pg(adev, true);
			gfx_v6_0_enable_gds_pg(adev, true);
		} else {
			WREG32(RLC_SAVE_AND_RESTORE_BASE, adev->gfx.rlc.save_restore_gpu_addr >> 8);
			WREG32(RLC_CLEAR_STATE_RESTORE_BASE, adev->gfx.rlc.clear_state_gpu_addr >> 8);

		}
		gfx_v6_0_init_ao_cu_mask(adev);
		gfx_v6_0_update_gfx_pg(adev, true);
	} else {

		WREG32(RLC_SAVE_AND_RESTORE_BASE, adev->gfx.rlc.save_restore_gpu_addr >> 8);
		WREG32(RLC_CLEAR_STATE_RESTORE_BASE, adev->gfx.rlc.clear_state_gpu_addr >> 8);
	}
}

static void gfx_v6_0_fini_pg(struct amdgpu_device *adev)
{
	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG |
			      AMD_PG_SUPPORT_CP |
			      AMD_PG_SUPPORT_GDS |
			      AMD_PG_SUPPORT_RLC_SMU_HS)) {
		gfx_v6_0_update_gfx_pg(adev, false);
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) {
			gfx_v6_0_enable_cp_pg(adev, false);
			gfx_v6_0_enable_gds_pg(adev, false);
		}
	}
}

static uint64_t gfx_v6_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32(RLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_LSB) |
	        ((uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	return clock;
}

static void gfx_v6_ring_emit_cntxcntl(struct amdgpu_ring *ring, uint32_t flags)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, 0x80000000);
	amdgpu_ring_write(ring, 0);
}

static const struct amdgpu_gfx_funcs gfx_v6_0_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v6_0_get_gpu_clock_counter,
	.select_se_sh = &gfx_v6_0_select_se_sh,
};

static int gfx_v6_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfx.num_gfx_rings = GFX6_NUM_GFX_RINGS;
	adev->gfx.num_compute_rings = GFX6_NUM_COMPUTE_RINGS;
	adev->gfx.funcs = &gfx_v6_0_gfx_funcs;
	gfx_v6_0_set_ring_funcs(adev);
	gfx_v6_0_set_irq_funcs(adev);

	return 0;
}

static int gfx_v6_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r;

	r = amdgpu_irq_add_id(adev, 181, &adev->gfx.eop_irq);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, 184, &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, 185, &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	gfx_v6_0_scratch_init(adev);

	r = gfx_v6_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load gfx firmware!\n");
		return r;
	}

	r = gfx_v6_0_rlc_init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->ring_obj = NULL;
		sprintf(ring->name, "gfx");
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->gfx.eop_irq, AMDGPU_CP_IRQ_GFX_EOP);
		if (r)
			return r;
	}

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		unsigned irq_type;

		if ((i >= 32) || (i >= AMDGPU_MAX_COMPUTE_RINGS)) {
			DRM_ERROR("Too many (%d) compute rings!\n", i);
			break;
		}
		ring = &adev->gfx.compute_ring[i];
		ring->ring_obj = NULL;
		ring->use_doorbell = false;
		ring->doorbell_index = 0;
		ring->me = 1;
		ring->pipe = i;
		ring->queue = i;
		sprintf(ring->name, "comp %d.%d.%d", ring->me, ring->pipe, ring->queue);
		irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP + ring->pipe;
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->gfx.eop_irq, irq_type);
		if (r)
			return r;
	}

	return r;
}

static int gfx_v6_0_sw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_bo_unref(&adev->gds.oa_gfx_bo);
	amdgpu_bo_unref(&adev->gds.gws_gfx_bo);
	amdgpu_bo_unref(&adev->gds.gds_gfx_bo);

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		amdgpu_ring_fini(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	gfx_v6_0_rlc_fini(adev);

	return 0;
}

static int gfx_v6_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfx_v6_0_gpu_init(adev);

	r = gfx_v6_0_rlc_resume(adev);
	if (r)
		return r;

	r = gfx_v6_0_cp_resume(adev);
	if (r)
		return r;

	adev->gfx.ce_ram_size = 0x8000;

	return r;
}

static int gfx_v6_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfx_v6_0_cp_enable(adev, false);
	gfx_v6_0_rlc_stop(adev);
	gfx_v6_0_fini_pg(adev);

	return 0;
}

static int gfx_v6_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gfx_v6_0_hw_fini(adev);
}

static int gfx_v6_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gfx_v6_0_hw_init(adev);
}

static bool gfx_v6_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (RREG32(GRBM_STATUS) & GRBM_STATUS__GUI_ACTIVE_MASK)
		return false;
	else
		return true;
}

static int gfx_v6_0_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v6_0_is_idle(handle))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v6_0_soft_reset(void *handle)
{
	return 0;
}

static void gfx_v6_0_set_gfx_eop_interrupt_state(struct amdgpu_device *adev,
						 enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl &= ~CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl |= CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}
}

static void gfx_v6_0_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
						     int ring,
						     enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;
	switch (state){
	case AMDGPU_IRQ_STATE_DISABLE:
		if (ring == 0) {
			cp_int_cntl = RREG32(CP_INT_CNTL_RING1);
			cp_int_cntl &= ~CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
			WREG32(CP_INT_CNTL_RING1, cp_int_cntl);
			break;
		} else {
			cp_int_cntl = RREG32(CP_INT_CNTL_RING2);
			cp_int_cntl &= ~CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
			WREG32(CP_INT_CNTL_RING2, cp_int_cntl);
			break;

		}
	case AMDGPU_IRQ_STATE_ENABLE:
		if (ring == 0) {
			cp_int_cntl = RREG32(CP_INT_CNTL_RING1);
			cp_int_cntl |= CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
			WREG32(CP_INT_CNTL_RING1, cp_int_cntl);
			break;
		} else {
			cp_int_cntl = RREG32(CP_INT_CNTL_RING2);
			cp_int_cntl |= CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK;
			WREG32(CP_INT_CNTL_RING2, cp_int_cntl);
			break;

		}

	default:
		BUG();
		break;

	}
}

static int gfx_v6_0_set_priv_reg_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *src,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl &= ~CP_INT_CNTL_RING0__PRIV_REG_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl |= CP_INT_CNTL_RING0__PRIV_REG_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v6_0_set_priv_inst_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *src,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl &= ~CP_INT_CNTL_RING0__PRIV_INSTR_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(CP_INT_CNTL_RING0);
		cp_int_cntl |= CP_INT_CNTL_RING0__PRIV_INSTR_INT_ENABLE_MASK;
		WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v6_0_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CP_IRQ_GFX_EOP:
		gfx_v6_0_set_gfx_eop_interrupt_state(adev, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
		gfx_v6_0_set_compute_eop_interrupt_state(adev, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
		gfx_v6_0_set_compute_eop_interrupt_state(adev, 1, state);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v6_0_eop_irq(struct amdgpu_device *adev,
			    struct amdgpu_irq_src *source,
			    struct amdgpu_iv_entry *entry)
{
	switch (entry->ring_id) {
	case 0:
		amdgpu_fence_process(&adev->gfx.gfx_ring[0]);
		break;
	case 1:
	case 2:
		amdgpu_fence_process(&adev->gfx.compute_ring[entry->ring_id -1]);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v6_0_priv_reg_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int gfx_v6_0_priv_inst_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int gfx_v6_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	bool gate = false;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (state == AMD_CG_STATE_GATE)
		gate = true;

	gfx_v6_0_enable_gui_idle_interrupt(adev, false);
	if (gate) {
		gfx_v6_0_enable_mgcg(adev, true);
		gfx_v6_0_enable_cgcg(adev, true);
	} else {
		gfx_v6_0_enable_cgcg(adev, false);
		gfx_v6_0_enable_mgcg(adev, false);
	}
	gfx_v6_0_enable_gui_idle_interrupt(adev, true);

	return 0;
}

static int gfx_v6_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	bool gate = false;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (state == AMD_PG_STATE_GATE)
		gate = true;

	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG |
			      AMD_PG_SUPPORT_CP |
			      AMD_PG_SUPPORT_GDS |
			      AMD_PG_SUPPORT_RLC_SMU_HS)) {
		gfx_v6_0_update_gfx_pg(adev, gate);
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) {
			gfx_v6_0_enable_cp_pg(adev, gate);
			gfx_v6_0_enable_gds_pg(adev, gate);
		}
	}

	return 0;
}

static const struct amd_ip_funcs gfx_v6_0_ip_funcs = {
	.name = "gfx_v6_0",
	.early_init = gfx_v6_0_early_init,
	.late_init = NULL,
	.sw_init = gfx_v6_0_sw_init,
	.sw_fini = gfx_v6_0_sw_fini,
	.hw_init = gfx_v6_0_hw_init,
	.hw_fini = gfx_v6_0_hw_fini,
	.suspend = gfx_v6_0_suspend,
	.resume = gfx_v6_0_resume,
	.is_idle = gfx_v6_0_is_idle,
	.wait_for_idle = gfx_v6_0_wait_for_idle,
	.soft_reset = gfx_v6_0_soft_reset,
	.set_clockgating_state = gfx_v6_0_set_clockgating_state,
	.set_powergating_state = gfx_v6_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs gfx_v6_0_ring_funcs_gfx = {
	.type = AMDGPU_RING_TYPE_GFX,
	.align_mask = 0xff,
	.nop = 0x80000000,
	.get_rptr = gfx_v6_0_ring_get_rptr,
	.get_wptr = gfx_v6_0_ring_get_wptr,
	.set_wptr = gfx_v6_0_ring_set_wptr_gfx,
	.emit_frame_size =
		5 + /* gfx_v6_0_ring_emit_hdp_flush */
		5 + /* gfx_v6_0_ring_emit_hdp_invalidate */
		14 + 14 + 14 + /* gfx_v6_0_ring_emit_fence x3 for user fence, vm fence */
		7 + 4 + /* gfx_v6_0_ring_emit_pipeline_sync */
		17 + 6 + /* gfx_v6_0_ring_emit_vm_flush */
		3, /* gfx_v6_ring_emit_cntxcntl */
	.emit_ib_size = 6, /* gfx_v6_0_ring_emit_ib */
	.emit_ib = gfx_v6_0_ring_emit_ib,
	.emit_fence = gfx_v6_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v6_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v6_0_ring_emit_vm_flush,
	.emit_hdp_flush = gfx_v6_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = gfx_v6_0_ring_emit_hdp_invalidate,
	.test_ring = gfx_v6_0_ring_test_ring,
	.test_ib = gfx_v6_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.emit_cntxcntl = gfx_v6_ring_emit_cntxcntl,
};

static const struct amdgpu_ring_funcs gfx_v6_0_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = 0x80000000,
	.get_rptr = gfx_v6_0_ring_get_rptr,
	.get_wptr = gfx_v6_0_ring_get_wptr,
	.set_wptr = gfx_v6_0_ring_set_wptr_compute,
	.emit_frame_size =
		5 + /* gfx_v6_0_ring_emit_hdp_flush */
		5 + /* gfx_v6_0_ring_emit_hdp_invalidate */
		7 + /* gfx_v6_0_ring_emit_pipeline_sync */
		17 + /* gfx_v6_0_ring_emit_vm_flush */
		14 + 14 + 14, /* gfx_v6_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 6, /* gfx_v6_0_ring_emit_ib */
	.emit_ib = gfx_v6_0_ring_emit_ib,
	.emit_fence = gfx_v6_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v6_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v6_0_ring_emit_vm_flush,
	.emit_hdp_flush = gfx_v6_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = gfx_v6_0_ring_emit_hdp_invalidate,
	.test_ring = gfx_v6_0_ring_test_ring,
	.test_ib = gfx_v6_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
};

static void gfx_v6_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		adev->gfx.gfx_ring[i].funcs = &gfx_v6_0_ring_funcs_gfx;
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		adev->gfx.compute_ring[i].funcs = &gfx_v6_0_ring_funcs_compute;
}

static const struct amdgpu_irq_src_funcs gfx_v6_0_eop_irq_funcs = {
	.set = gfx_v6_0_set_eop_interrupt_state,
	.process = gfx_v6_0_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v6_0_priv_reg_irq_funcs = {
	.set = gfx_v6_0_set_priv_reg_fault_state,
	.process = gfx_v6_0_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v6_0_priv_inst_irq_funcs = {
	.set = gfx_v6_0_set_priv_inst_fault_state,
	.process = gfx_v6_0_priv_inst_irq,
};

static void gfx_v6_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v6_0_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v6_0_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v6_0_priv_inst_irq_funcs;
}

static void gfx_v6_0_get_cu_info(struct amdgpu_device *adev)
{
	int i, j, k, counter, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0;
	struct amdgpu_cu_info *cu_info = &adev->gfx.cu_info;

	memset(cu_info, 0, sizeof(*cu_info));

	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			ao_bitmap = 0;
			counter = 0;
			bitmap = gfx_v6_0_get_cu_active_bitmap(adev, i, j);
			cu_info->bitmap[i][j] = bitmap;

			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k ++) {
				if (bitmap & mask) {
					if (counter < 2)
						ao_bitmap |= mask;
					counter ++;
				}
				mask <<= 1;
			}
			active_cu_number += counter;
			ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
		}
	}

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
}

const struct amdgpu_ip_block_version gfx_v6_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 6,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v6_0_ip_funcs,
};
