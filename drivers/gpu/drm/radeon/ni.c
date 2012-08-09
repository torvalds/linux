/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
#include "radeon_drm.h"
#include "nid.h"
#include "atom.h"
#include "ni_reg.h"
#include "cayman_blit_shaders.h"

extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern int evergreen_mc_wait_for_idle(struct radeon_device *rdev);
extern void evergreen_mc_program(struct radeon_device *rdev);
extern void evergreen_irq_suspend(struct radeon_device *rdev);
extern int evergreen_mc_init(struct radeon_device *rdev);
extern void evergreen_fix_pci_max_read_req_size(struct radeon_device *rdev);
extern void evergreen_pcie_gen2_enable(struct radeon_device *rdev);
extern void si_rlc_fini(struct radeon_device *rdev);
extern int si_rlc_init(struct radeon_device *rdev);

#define EVERGREEN_PFP_UCODE_SIZE 1120
#define EVERGREEN_PM4_UCODE_SIZE 1376
#define EVERGREEN_RLC_UCODE_SIZE 768
#define BTC_MC_UCODE_SIZE 6024

#define CAYMAN_PFP_UCODE_SIZE 2176
#define CAYMAN_PM4_UCODE_SIZE 2176
#define CAYMAN_RLC_UCODE_SIZE 1024
#define CAYMAN_MC_UCODE_SIZE 6037

#define ARUBA_RLC_UCODE_SIZE 1536

/* Firmware Names */
MODULE_FIRMWARE("radeon/BARTS_pfp.bin");
MODULE_FIRMWARE("radeon/BARTS_me.bin");
MODULE_FIRMWARE("radeon/BARTS_mc.bin");
MODULE_FIRMWARE("radeon/BTC_rlc.bin");
MODULE_FIRMWARE("radeon/TURKS_pfp.bin");
MODULE_FIRMWARE("radeon/TURKS_me.bin");
MODULE_FIRMWARE("radeon/TURKS_mc.bin");
MODULE_FIRMWARE("radeon/CAICOS_pfp.bin");
MODULE_FIRMWARE("radeon/CAICOS_me.bin");
MODULE_FIRMWARE("radeon/CAICOS_mc.bin");
MODULE_FIRMWARE("radeon/CAYMAN_pfp.bin");
MODULE_FIRMWARE("radeon/CAYMAN_me.bin");
MODULE_FIRMWARE("radeon/CAYMAN_mc.bin");
MODULE_FIRMWARE("radeon/CAYMAN_rlc.bin");
MODULE_FIRMWARE("radeon/ARUBA_pfp.bin");
MODULE_FIRMWARE("radeon/ARUBA_me.bin");
MODULE_FIRMWARE("radeon/ARUBA_rlc.bin");

#define BTC_IO_MC_REGS_SIZE 29

static const u32 barts_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00946a00}
};

static const u32 turks_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00936a00}
};

static const u32 caicos_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00916a00}
};

static const u32 cayman_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00976b00}
};

int ni_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	u32 mem_type, running, blackout = 0;
	u32 *io_mc_regs;
	int i, ucode_size, regs_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	switch (rdev->family) {
	case CHIP_BARTS:
		io_mc_regs = (u32 *)&barts_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_TURKS:
		io_mc_regs = (u32 *)&turks_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_CAICOS:
	default:
		io_mc_regs = (u32 *)&caicos_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_CAYMAN:
		io_mc_regs = (u32 *)&cayman_io_mc_regs;
		ucode_size = CAYMAN_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	}

	mem_type = (RREG32(MC_SEQ_MISC0) & MC_SEQ_MISC0_GDDR5_MASK) >> MC_SEQ_MISC0_GDDR5_SHIFT;
	running = RREG32(MC_SEQ_SUP_CNTL) & RUN_MASK;

	if ((mem_type == MC_SEQ_MISC0_GDDR5_VALUE) && (running == 0)) {
		if (running) {
			blackout = RREG32(MC_SHARED_BLACKOUT_CNTL);
			WREG32(MC_SHARED_BLACKOUT_CNTL, 1);
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
			if (RREG32(MC_IO_PAD_CNTL_D0) & MEM_FALL_OUT_CMD)
				break;
			udelay(1);
		}

		if (running)
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout);
	}

	return 0;
}

int ni_init_microcode(struct radeon_device *rdev)
{
	struct platform_device *pdev;
	const char *chip_name;
	const char *rlc_chip_name;
	size_t pfp_req_size, me_req_size, rlc_req_size, mc_req_size;
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
	case CHIP_BARTS:
		chip_name = "BARTS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		break;
	case CHIP_TURKS:
		chip_name = "TURKS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		break;
	case CHIP_CAICOS:
		chip_name = "CAICOS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		break;
	case CHIP_CAYMAN:
		chip_name = "CAYMAN";
		rlc_chip_name = "CAYMAN";
		pfp_req_size = CAYMAN_PFP_UCODE_SIZE * 4;
		me_req_size = CAYMAN_PM4_UCODE_SIZE * 4;
		rlc_req_size = CAYMAN_RLC_UCODE_SIZE * 4;
		mc_req_size = CAYMAN_MC_UCODE_SIZE * 4;
		break;
	case CHIP_ARUBA:
		chip_name = "ARUBA";
		rlc_chip_name = "ARUBA";
		/* pfp/me same size as CAYMAN */
		pfp_req_size = CAYMAN_PFP_UCODE_SIZE * 4;
		me_req_size = CAYMAN_PM4_UCODE_SIZE * 4;
		rlc_req_size = ARUBA_RLC_UCODE_SIZE * 4;
		mc_req_size = 0;
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
		       "ni_cp: Bogus length %zu in firmware \"%s\"\n",
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
		       "ni_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", rlc_chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->rlc_fw->size != rlc_req_size) {
		printk(KERN_ERR
		       "ni_rlc: Bogus length %zu in firmware \"%s\"\n",
		       rdev->rlc_fw->size, fw_name);
		err = -EINVAL;
	}

	/* no MC ucode on TN */
	if (!(rdev->flags & RADEON_IS_IGP)) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
		err = request_firmware(&rdev->mc_fw, fw_name, &pdev->dev);
		if (err)
			goto out;
		if (rdev->mc_fw->size != mc_req_size) {
			printk(KERN_ERR
			       "ni_mc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mc_fw->size, fw_name);
			err = -EINVAL;
		}
	}
out:
	platform_device_unregister(pdev);

	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "ni_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
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
static void cayman_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = 0;
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 cgts_tcc_disable;
	u32 sx_debug_1;
	u32 smx_dc_ctl0;
	u32 cgts_sm_ctrl_reg;
	u32 hdp_host_path_cntl;
	u32 tmp;
	u32 disabled_rb_mask;
	int i, j;

	switch (rdev->family) {
	case CHIP_CAYMAN:
		rdev->config.cayman.max_shader_engines = 2;
		rdev->config.cayman.max_pipes_per_simd = 4;
		rdev->config.cayman.max_tile_pipes = 8;
		rdev->config.cayman.max_simds_per_se = 12;
		rdev->config.cayman.max_backends_per_se = 4;
		rdev->config.cayman.max_texture_channel_caches = 8;
		rdev->config.cayman.max_gprs = 256;
		rdev->config.cayman.max_threads = 256;
		rdev->config.cayman.max_gs_threads = 32;
		rdev->config.cayman.max_stack_entries = 512;
		rdev->config.cayman.sx_num_of_sets = 8;
		rdev->config.cayman.sx_max_export_size = 256;
		rdev->config.cayman.sx_max_export_pos_size = 64;
		rdev->config.cayman.sx_max_export_smx_size = 192;
		rdev->config.cayman.max_hw_contexts = 8;
		rdev->config.cayman.sq_num_cf_insts = 2;

		rdev->config.cayman.sc_prim_fifo_size = 0x100;
		rdev->config.cayman.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cayman.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CAYMAN_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_ARUBA:
	default:
		rdev->config.cayman.max_shader_engines = 1;
		rdev->config.cayman.max_pipes_per_simd = 4;
		rdev->config.cayman.max_tile_pipes = 2;
		if ((rdev->pdev->device == 0x9900) ||
		    (rdev->pdev->device == 0x9901) ||
		    (rdev->pdev->device == 0x9905) ||
		    (rdev->pdev->device == 0x9906) ||
		    (rdev->pdev->device == 0x9907) ||
		    (rdev->pdev->device == 0x9908) ||
		    (rdev->pdev->device == 0x9909) ||
		    (rdev->pdev->device == 0x9910) ||
		    (rdev->pdev->device == 0x9917)) {
			rdev->config.cayman.max_simds_per_se = 6;
			rdev->config.cayman.max_backends_per_se = 2;
		} else if ((rdev->pdev->device == 0x9903) ||
			   (rdev->pdev->device == 0x9904) ||
			   (rdev->pdev->device == 0x990A) ||
			   (rdev->pdev->device == 0x9913) ||
			   (rdev->pdev->device == 0x9918)) {
			rdev->config.cayman.max_simds_per_se = 4;
			rdev->config.cayman.max_backends_per_se = 2;
		} else if ((rdev->pdev->device == 0x9919) ||
			   (rdev->pdev->device == 0x9990) ||
			   (rdev->pdev->device == 0x9991) ||
			   (rdev->pdev->device == 0x9994) ||
			   (rdev->pdev->device == 0x99A0)) {
			rdev->config.cayman.max_simds_per_se = 3;
			rdev->config.cayman.max_backends_per_se = 1;
		} else {
			rdev->config.cayman.max_simds_per_se = 2;
			rdev->config.cayman.max_backends_per_se = 1;
		}
		rdev->config.cayman.max_texture_channel_caches = 2;
		rdev->config.cayman.max_gprs = 256;
		rdev->config.cayman.max_threads = 256;
		rdev->config.cayman.max_gs_threads = 32;
		rdev->config.cayman.max_stack_entries = 512;
		rdev->config.cayman.sx_num_of_sets = 8;
		rdev->config.cayman.sx_max_export_size = 256;
		rdev->config.cayman.sx_max_export_pos_size = 64;
		rdev->config.cayman.sx_max_export_smx_size = 192;
		rdev->config.cayman.max_hw_contexts = 8;
		rdev->config.cayman.sq_num_cf_insts = 2;

		rdev->config.cayman.sc_prim_fifo_size = 0x40;
		rdev->config.cayman.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cayman.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = ARUBA_GB_ADDR_CONFIG_GOLDEN;
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

	evergreen_fix_pci_max_read_req_size(rdev);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.cayman.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.cayman.mem_row_size_in_kb > 4)
		rdev->config.cayman.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.cayman.shader_engine_tile_size = 32;
	rdev->config.cayman.num_gpus = 1;
	rdev->config.cayman.multi_gpu_tile_size = 64;

	tmp = (gb_addr_config & NUM_PIPES_MASK) >> NUM_PIPES_SHIFT;
	rdev->config.cayman.num_tile_pipes = (1 << tmp);
	tmp = (gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT;
	rdev->config.cayman.mem_max_burst_length_bytes = (tmp + 1) * 256;
	tmp = (gb_addr_config & NUM_SHADER_ENGINES_MASK) >> NUM_SHADER_ENGINES_SHIFT;
	rdev->config.cayman.num_shader_engines = tmp + 1;
	tmp = (gb_addr_config & NUM_GPUS_MASK) >> NUM_GPUS_SHIFT;
	rdev->config.cayman.num_gpus = tmp + 1;
	tmp = (gb_addr_config & MULTI_GPU_TILE_SIZE_MASK) >> MULTI_GPU_TILE_SIZE_SHIFT;
	rdev->config.cayman.multi_gpu_tile_size = 1 << tmp;
	tmp = (gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT;
	rdev->config.cayman.mem_row_size_in_kb = 1 << tmp;


	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.cayman.tile_config = 0;
	switch (rdev->config.cayman.num_tile_pipes) {
	case 1:
	default:
		rdev->config.cayman.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.cayman.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.cayman.tile_config |= (2 << 0);
		break;
	case 8:
		rdev->config.cayman.tile_config |= (3 << 0);
		break;
	}

	/* num banks is 8 on all fusion asics. 0 = 4, 1 = 8, 2 = 16 */
	if (rdev->flags & RADEON_IS_IGP)
		rdev->config.cayman.tile_config |= 1 << 4;
	else {
		switch ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) {
		case 0: /* four banks */
			rdev->config.cayman.tile_config |= 0 << 4;
			break;
		case 1: /* eight banks */
			rdev->config.cayman.tile_config |= 1 << 4;
			break;
		case 2: /* sixteen banks */
		default:
			rdev->config.cayman.tile_config |= 2 << 4;
			break;
		}
	}
	rdev->config.cayman.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.cayman.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	tmp = 0;
	for (i = (rdev->config.cayman.max_shader_engines - 1); i >= 0; i--) {
		u32 rb_disable_bitmap;

		WREG32(GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		WREG32(RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		rb_disable_bitmap = (RREG32(CC_RB_BACKEND_DISABLE) & 0x00ff0000) >> 16;
		tmp <<= 4;
		tmp |= rb_disable_bitmap;
	}
	/* enabled rb are just the one not disabled :) */
	disabled_rb_mask = tmp;

	WREG32(GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);
	WREG32(RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CONFIG, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);

	tmp = gb_addr_config & NUM_PIPES_MASK;
	tmp = r6xx_remap_render_backend(rdev, tmp,
					rdev->config.cayman.max_backends_per_se *
					rdev->config.cayman.max_shader_engines,
					CAYMAN_MAX_BACKENDS, disabled_rb_mask);
	WREG32(GB_BACKEND_MAP, tmp);

	cgts_tcc_disable = 0xffff0000;
	for (i = 0; i < rdev->config.cayman.max_texture_channel_caches; i++)
		cgts_tcc_disable &= ~(1 << (16 + i));
	WREG32(CGTS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_SYS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_USER_SYS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_USER_TCC_DISABLE, cgts_tcc_disable);

	/* reprogram the shader complex */
	cgts_sm_ctrl_reg = RREG32(CGTS_SM_CTRL_REG);
	for (i = 0; i < 16; i++)
		WREG32(CGTS_SM_CTRL_REG, OVERRIDE);
	WREG32(CGTS_SM_CTRL_REG, cgts_sm_ctrl_reg);

	/* set HW defaults for 3D engine */
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	sx_debug_1 = RREG32(SX_DEBUG_1);
	sx_debug_1 |= ENABLE_NEW_SMX_ADDRESS;
	WREG32(SX_DEBUG_1, sx_debug_1);

	smx_dc_ctl0 = RREG32(SMX_DC_CTL0);
	smx_dc_ctl0 &= ~NUMBER_OF_SETS(0x1ff);
	smx_dc_ctl0 |= NUMBER_OF_SETS(rdev->config.cayman.sx_num_of_sets);
	WREG32(SMX_DC_CTL0, smx_dc_ctl0);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4) | CRC_SIMD_ID_WADDR_DISABLE);

	/* need to be explicitly zero-ed */
	WREG32(VGT_OFFCHIP_LDS_BASE, 0);
	WREG32(SQ_LSTMP_RING_BASE, 0);
	WREG32(SQ_HSTMP_RING_BASE, 0);
	WREG32(SQ_ESTMP_RING_BASE, 0);
	WREG32(SQ_GSTMP_RING_BASE, 0);
	WREG32(SQ_VSTMP_RING_BASE, 0);
	WREG32(SQ_PSTMP_RING_BASE, 0);

	WREG32(TA_CNTL_AUX, DISABLE_CUBE_ANISO);

	WREG32(SX_EXPORT_BUFFER_SIZES, (COLOR_BUFFER_SIZE((rdev->config.cayman.sx_max_export_size / 4) - 1) |
					POSITION_BUFFER_SIZE((rdev->config.cayman.sx_max_export_pos_size / 4) - 1) |
					SMX_BUFFER_SIZE((rdev->config.cayman.sx_max_export_smx_size / 4) - 1)));

	WREG32(PA_SC_FIFO_SIZE, (SC_PRIM_FIFO_SIZE(rdev->config.cayman.sc_prim_fifo_size) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.cayman.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.cayman.sc_earlyz_tile_fifo_size)));


	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_MS_FIFO_SIZES, (CACHE_FIFO_SIZE(16 * rdev->config.cayman.sq_num_cf_insts) |
				  FETCH_FIFO_HIWATER(0x4) |
				  DONE_FIFO_HIWATER(0xe0) |
				  ALU_UPDATE_FIFO_HIWATER(0x8)));

	WREG32(SQ_GPR_RESOURCE_MGMT_1, NUM_CLAUSE_TEMP_GPRS(4));
	WREG32(SQ_CONFIG, (VC_ENABLE |
			   EXPORT_SRC_C |
			   GFX_PRIO(0) |
			   CS1_PRIO(0) |
			   CS2_PRIO(1)));
	WREG32(SQ_DYN_GPR_CNTL_PS_FLUSH_REQ, DYN_GPR_ENABLE);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	WREG32(CB_PERF_CTR0_SEL_0, 0);
	WREG32(CB_PERF_CTR0_SEL_1, 0);
	WREG32(CB_PERF_CTR1_SEL_0, 0);
	WREG32(CB_PERF_CTR1_SEL_1, 0);
	WREG32(CB_PERF_CTR2_SEL_0, 0);
	WREG32(CB_PERF_CTR2_SEL_1, 0);
	WREG32(CB_PERF_CTR3_SEL_0, 0);
	WREG32(CB_PERF_CTR3_SEL_1, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	udelay(50);
}

/*
 * GART
 */
void cayman_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);

	/* bits 0-7 are the VM contexts0-7 */
	WREG32(VM_INVALIDATE_REQUEST, 1);
}

int cayman_pcie_gart_enable(struct radeon_device *rdev)
{
	int i, r;

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
	       ENABLE_L1_FRAGMENT_PROCESSING |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       ENABLE_ADVANCED_DRIVER_MODEL |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE |
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
	WREG32(VM_CONTEXT0_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);

	WREG32(0x15D4, 0);
	WREG32(0x15D8, 0);
	WREG32(0x15DC, 0);

	/* empty context1-7 */
	for (i = 1; i < 8; i++) {
		WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR + (i << 2), 0);
		WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR + (i << 2), 0);
		WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
			rdev->gart.table_addr >> 12);
	}

	/* enable context1-7 */
	WREG32(VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT1_CNTL2, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);

	cayman_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

void cayman_pcie_gart_disable(struct radeon_device *rdev)
{
	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_FRAGMENT_PROCESSING |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	radeon_gart_table_vram_unpin(rdev);
}

void cayman_pcie_gart_fini(struct radeon_device *rdev)
{
	cayman_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

void cayman_cp_int_cntl_setup(struct radeon_device *rdev,
			      int ring, u32 cp_int_cntl)
{
	u32 srbm_gfx_cntl = RREG32(SRBM_GFX_CNTL) & ~3;

	WREG32(SRBM_GFX_CNTL, srbm_gfx_cntl | (ring & 3));
	WREG32(CP_INT_CNTL, cp_int_cntl);
}

/*
 * CP.
 */
void cayman_fence_ring_emit(struct radeon_device *rdev,
			    struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* flush read cache over gart for this vmid */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_TC_ACTION_ENA | PACKET3_SH_ACTION_ENA);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 10); /* poll interval */
	/* EVENT_WRITE_EOP - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, EVENT_TYPE(CACHE_FLUSH_AND_INV_EVENT_TS) | EVENT_INDEX(5));
	radeon_ring_write(ring, addr & 0xffffffff);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

void cayman_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];

	/* set to DX10/11 mode */
	radeon_ring_write(ring, PACKET3(PACKET3_MODE_CONTROL, 0));
	radeon_ring_write(ring, 1);

	if (ring->rptr_save_reg) {
		uint32_t next_rptr = ring->wptr + 3 + 4 + 8;
		radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(ring, ((ring->rptr_save_reg - 
					  PACKET3_SET_CONFIG_REG_START) >> 2));
		radeon_ring_write(ring, next_rptr);
	}

	radeon_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFF);
	radeon_ring_write(ring, ib->length_dw | 
			  (ib->vm ? (ib->vm->id << 24) : 0));

	/* flush read cache over gart for this vmid */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
	radeon_ring_write(ring, ib->vm ? ib->vm->id : 0);
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_TC_ACTION_ENA | PACKET3_SH_ACTION_ENA);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 10); /* poll interval */
}

static void cayman_cp_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT));
		WREG32(SCRATCH_UMSK, 0);
	}
}

static int cayman_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw)
		return -EINVAL;

	cayman_cp_enable(rdev, false);

	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < CAYMAN_PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < CAYMAN_PM4_UCODE_SIZE; i++)
		WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

static int cayman_cp_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	r = radeon_ring_lock(rdev, ring, 7);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}
	radeon_ring_write(ring, PACKET3(PACKET3_ME_INITIALIZE, 5));
	radeon_ring_write(ring, 0x1);
	radeon_ring_write(ring, 0x0);
	radeon_ring_write(ring, rdev->config.cayman.max_hw_contexts - 1);
	radeon_ring_write(ring, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);
	radeon_ring_unlock_commit(rdev, ring);

	cayman_cp_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, cayman_default_size + 19);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (i = 0; i < cayman_default_size; i++)
		radeon_ring_write(ring, cayman_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	/* SQ_VTX_BASE_VTX_LOC */
	radeon_ring_write(ring, 0xc0026f00);
	radeon_ring_write(ring, 0x00000000);
	radeon_ring_write(ring, 0x00000000);
	radeon_ring_write(ring, 0x00000000);

	/* Clear consts */
	radeon_ring_write(ring, 0xc0036f00);
	radeon_ring_write(ring, 0x00000bc4);
	radeon_ring_write(ring, 0xffffffff);
	radeon_ring_write(ring, 0xffffffff);
	radeon_ring_write(ring, 0xffffffff);

	radeon_ring_write(ring, 0xc0026900);
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /*  */

	radeon_ring_unlock_commit(rdev, ring);

	/* XXX init other rings */

	return 0;
}

static void cayman_cp_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	cayman_cp_enable(rdev, false);
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);
}

int cayman_cp_resume(struct radeon_device *rdev)
{
	static const int ridx[] = {
		RADEON_RING_TYPE_GFX_INDEX,
		CAYMAN_RING_TYPE_CP1_INDEX,
		CAYMAN_RING_TYPE_CP2_INDEX
	};
	static const unsigned cp_rb_cntl[] = {
		CP_RB0_CNTL,
		CP_RB1_CNTL,
		CP_RB2_CNTL,
	};
	static const unsigned cp_rb_rptr_addr[] = {
		CP_RB0_RPTR_ADDR,
		CP_RB1_RPTR_ADDR,
		CP_RB2_RPTR_ADDR
	};
	static const unsigned cp_rb_rptr_addr_hi[] = {
		CP_RB0_RPTR_ADDR_HI,
		CP_RB1_RPTR_ADDR_HI,
		CP_RB2_RPTR_ADDR_HI
	};
	static const unsigned cp_rb_base[] = {
		CP_RB0_BASE,
		CP_RB1_BASE,
		CP_RB2_BASE
	};
	struct radeon_ring *ring;
	int i, r;

	/* Reset cp; if cp is reset, then PA, SH, VGT also need to be reset */
	WREG32(GRBM_SOFT_RESET, (SOFT_RESET_CP |
				 SOFT_RESET_PA |
				 SOFT_RESET_SH |
				 SOFT_RESET_VGT |
				 SOFT_RESET_SPI |
				 SOFT_RESET_SX));
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);
	RREG32(GRBM_SOFT_RESET);

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	WREG32(CP_DEBUG, (1 << 27));

	/* set the wb address wether it's enabled or not */
	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);
	WREG32(SCRATCH_UMSK, 0xff);

	for (i = 0; i < 3; ++i) {
		uint32_t rb_cntl;
		uint64_t addr;

		/* Set ring buffer size */
		ring = &rdev->ring[ridx[i]];
		rb_cntl = drm_order(ring->ring_size / 8);
		rb_cntl |= drm_order(RADEON_GPU_PAGE_SIZE/8) << 8;
#ifdef __BIG_ENDIAN
		rb_cntl |= BUF_SWAP_32BIT;
#endif
		WREG32(cp_rb_cntl[i], rb_cntl);

		/* set the wb address wether it's enabled or not */
		addr = rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET;
		WREG32(cp_rb_rptr_addr[i], addr & 0xFFFFFFFC);
		WREG32(cp_rb_rptr_addr_hi[i], upper_32_bits(addr) & 0xFF);
	}

	/* set the rb base addr, this causes an internal reset of ALL rings */
	for (i = 0; i < 3; ++i) {
		ring = &rdev->ring[ridx[i]];
		WREG32(cp_rb_base[i], ring->gpu_addr >> 8);
	}

	for (i = 0; i < 3; ++i) {
		/* Initialize the ring buffer's read and write pointers */
		ring = &rdev->ring[ridx[i]];
		WREG32_P(cp_rb_cntl[i], RB_RPTR_WR_ENA, ~RB_RPTR_WR_ENA);

		ring->rptr = ring->wptr = 0;
		WREG32(ring->rptr_reg, ring->rptr);
		WREG32(ring->wptr_reg, ring->wptr);

		mdelay(1);
		WREG32_P(cp_rb_cntl[i], 0, ~RB_RPTR_WR_ENA);
	}

	/* start the rings */
	cayman_cp_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
	rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	/* this only test cp0 */
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
		return r;
	}

	return 0;
}

static int cayman_gpu_soft_reset(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 grbm_reset = 0;

	if (!(RREG32(GRBM_STATUS) & GUI_ACTIVE))
		return 0;

	dev_info(rdev->dev, "GPU softreset \n");
	dev_info(rdev->dev, "  GRBM_STATUS=0x%08X\n",
		RREG32(GRBM_STATUS));
	dev_info(rdev->dev, "  GRBM_STATUS_SE0=0x%08X\n",
		RREG32(GRBM_STATUS_SE0));
	dev_info(rdev->dev, "  GRBM_STATUS_SE1=0x%08X\n",
		RREG32(GRBM_STATUS_SE1));
	dev_info(rdev->dev, "  SRBM_STATUS=0x%08X\n",
		RREG32(SRBM_STATUS));
	dev_info(rdev->dev, "  R_008674_CP_STALLED_STAT1 = 0x%08X\n",
		RREG32(CP_STALLED_STAT1));
	dev_info(rdev->dev, "  R_008678_CP_STALLED_STAT2 = 0x%08X\n",
		RREG32(CP_STALLED_STAT2));
	dev_info(rdev->dev, "  R_00867C_CP_BUSY_STAT     = 0x%08X\n",
		RREG32(CP_BUSY_STAT));
	dev_info(rdev->dev, "  R_008680_CP_STAT          = 0x%08X\n",
		RREG32(CP_STAT));
	dev_info(rdev->dev, "  VM_CONTEXT0_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(0x14F8));
	dev_info(rdev->dev, "  VM_CONTEXT0_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(0x14D8));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(0x14FC));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(0x14DC));

	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT);

	/* reset all the gfx blocks */
	grbm_reset = (SOFT_RESET_CP |
		      SOFT_RESET_CB |
		      SOFT_RESET_DB |
		      SOFT_RESET_GDS |
		      SOFT_RESET_PA |
		      SOFT_RESET_SC |
		      SOFT_RESET_SPI |
		      SOFT_RESET_SH |
		      SOFT_RESET_SX |
		      SOFT_RESET_TC |
		      SOFT_RESET_TA |
		      SOFT_RESET_VGT |
		      SOFT_RESET_IA);

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
	dev_info(rdev->dev, "  GRBM_STATUS_SE0=0x%08X\n",
		RREG32(GRBM_STATUS_SE0));
	dev_info(rdev->dev, "  GRBM_STATUS_SE1=0x%08X\n",
		RREG32(GRBM_STATUS_SE1));
	dev_info(rdev->dev, "  SRBM_STATUS=0x%08X\n",
		RREG32(SRBM_STATUS));
	dev_info(rdev->dev, "  R_008674_CP_STALLED_STAT1 = 0x%08X\n",
		RREG32(CP_STALLED_STAT1));
	dev_info(rdev->dev, "  R_008678_CP_STALLED_STAT2 = 0x%08X\n",
		RREG32(CP_STALLED_STAT2));
	dev_info(rdev->dev, "  R_00867C_CP_BUSY_STAT     = 0x%08X\n",
		RREG32(CP_BUSY_STAT));
	dev_info(rdev->dev, "  R_008680_CP_STAT          = 0x%08X\n",
		RREG32(CP_STAT));
	evergreen_mc_resume(rdev, &save);
	return 0;
}

int cayman_asic_reset(struct radeon_device *rdev)
{
	return cayman_gpu_soft_reset(rdev);
}

static int cayman_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r;

	/* enable pcie gen2 link */
	evergreen_pcie_gen2_enable(rdev);

	if (rdev->flags & RADEON_IS_IGP) {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->rlc_fw) {
			r = ni_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	} else {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->rlc_fw || !rdev->mc_fw) {
			r = ni_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}

		r = ni_mc_load_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	evergreen_mc_program(rdev);
	r = cayman_pcie_gart_enable(rdev);
	if (r)
		return r;
	cayman_gpu_init(rdev);

	r = evergreen_blit_init(rdev);
	if (r) {
		r600_blit_fini(rdev);
		rdev->asic->copy.copy = NULL;
		dev_warn(rdev->dev, "failed blitter (%d) falling back to memcpy\n", r);
	}

	/* allocate rlc buffers */
	if (rdev->flags & RADEON_IS_IGP) {
		r = si_rlc_init(rdev);
		if (r) {
			DRM_ERROR("Failed to init rlc BOs!\n");
			return r;
		}
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

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	r = r600_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	evergreen_irq_set(rdev);

	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     CP_RB0_RPTR, CP_RB0_WPTR,
			     0, 0xfffff, RADEON_CP_PACKET2);
	if (r)
		return r;
	r = cayman_cp_load_microcode(rdev);
	if (r)
		return r;
	r = cayman_cp_resume(rdev);
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

	r = r600_audio_init(rdev);
	if (r)
		return r;

	return 0;
}

int cayman_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on rv770 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	rdev->accel_working = true;
	r = cayman_startup(rdev);
	if (r) {
		DRM_ERROR("cayman startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}
	return r;
}

int cayman_suspend(struct radeon_device *rdev)
{
	r600_audio_fini(rdev);
	cayman_cp_enable(rdev, false);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	evergreen_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	cayman_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int cayman_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
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
	r600_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* initialize memory controller */
	r = evergreen_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	r = radeon_irq_kms_init(rdev);
	if (r)
		return r;

	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = cayman_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		cayman_cp_fini(rdev);
		r600_irq_fini(rdev);
		if (rdev->flags & RADEON_IS_IGP)
			si_rlc_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		cayman_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 *
	 * We can skip this check for TN, because there is no MC
	 * ucode.
	 */
	if (!rdev->mc_fw && !(rdev->flags & RADEON_IS_IGP)) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

void cayman_fini(struct radeon_device *rdev)
{
	r600_blit_fini(rdev);
	cayman_cp_fini(rdev);
	r600_irq_fini(rdev);
	if (rdev->flags & RADEON_IS_IGP)
		si_rlc_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	cayman_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/*
 * vm
 */
int cayman_vm_init(struct radeon_device *rdev)
{
	/* number of VMs */
	rdev->vm_manager.nvm = 8;
	/* base offset of vram pages */
	if (rdev->flags & RADEON_IS_IGP) {
		u64 tmp = RREG32(FUS_MC_VM_FB_OFFSET);
		tmp <<= 22;
		rdev->vm_manager.vram_base_offset = tmp;
	} else
		rdev->vm_manager.vram_base_offset = 0;
	return 0;
}

void cayman_vm_fini(struct radeon_device *rdev)
{
}

#define R600_PTE_VALID     (1 << 0)
#define R600_PTE_SYSTEM    (1 << 1)
#define R600_PTE_SNOOPED   (1 << 2)
#define R600_PTE_READABLE  (1 << 5)
#define R600_PTE_WRITEABLE (1 << 6)

uint32_t cayman_vm_page_flags(struct radeon_device *rdev,
			      struct radeon_vm *vm,
			      uint32_t flags)
{
	uint32_t r600_flags = 0;

	r600_flags |= (flags & RADEON_VM_PAGE_VALID) ? R600_PTE_VALID : 0;
	r600_flags |= (flags & RADEON_VM_PAGE_READABLE) ? R600_PTE_READABLE : 0;
	r600_flags |= (flags & RADEON_VM_PAGE_WRITEABLE) ? R600_PTE_WRITEABLE : 0;
	if (flags & RADEON_VM_PAGE_SYSTEM) {
		r600_flags |= R600_PTE_SYSTEM;
		r600_flags |= (flags & RADEON_VM_PAGE_SNOOPED) ? R600_PTE_SNOOPED : 0;
	}
	return r600_flags;
}

void cayman_vm_set_page(struct radeon_device *rdev, struct radeon_vm *vm,
			unsigned pfn, uint64_t addr, uint32_t flags)
{
	void __iomem *ptr = (void *)vm->pt;

	addr = addr & 0xFFFFFFFFFFFFF000ULL;
	addr |= flags;
	writeq(addr, ptr + (pfn * 8));
}

void cayman_vm_flush(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	struct radeon_vm *vm = ib->vm;

	if (vm == NULL)
		return;

	radeon_ring_write(ring, PACKET0(VM_CONTEXT0_PAGE_TABLE_START_ADDR + (vm->id << 2), 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET0(VM_CONTEXT0_PAGE_TABLE_END_ADDR + (vm->id << 2), 0));
	radeon_ring_write(ring, vm->last_pfn);

	radeon_ring_write(ring, PACKET0(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2), 0));
	radeon_ring_write(ring, vm->pt_gpu_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, PACKET0(HDP_MEM_COHERENCY_FLUSH_CNTL, 0));
	radeon_ring_write(ring, 0x1);

	/* bits 0-7 are the VM contexts0-7 */
	radeon_ring_write(ring, PACKET0(VM_INVALIDATE_REQUEST, 0));
	radeon_ring_write(ring, 1 << ib->vm->id);
}
