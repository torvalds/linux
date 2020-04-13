/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v13_0.h"

#include "mp/mp_13_0_2_offset.h"
#include "mp/mp_13_0_2_sh_mask.h"

MODULE_FIRMWARE("amdgpu/aldebaran_sos.bin");

static int psp_v13_0_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	const char *chip_name;
	int err = 0;

	switch (adev->asic_type) {
	case CHIP_ALDEBARAN:
		chip_name = "aldebaran";
		break;
	default:
		BUG();
	}

	err = psp_init_sos_microcode(psp, chip_name);

	return err;
}

static bool psp_v13_0_is_sos_alive(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg;

	sol_reg = RREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_81);

	return sol_reg != 0x0;
}

static int psp_v13_0_wait_for_bootloader(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	int ret;
	int retry_loop;

	for (retry_loop = 0; retry_loop < 10; retry_loop++) {
		/* Wait for bootloader to signify that is
		    ready having bit 31 of C2PMSG_35 set to 1 */
		ret = psp_wait_for(psp,
				   SOC15_REG_OFFSET(MP0, 0, regMP0_SMN_C2PMSG_35),
				   0x80000000,
				   0x80000000,
				   false);

		if (ret == 0)
			return 0;
	}

	return ret;
}

static int psp_v13_0_bootloader_load_kdb(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check tOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v13_0_is_sos_alive(psp))
		return 0;

	ret = psp_v13_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP KDB binary to memory */
	memcpy(psp->fw_pri_buf, psp->kdb_start_addr, psp->kdb_bin_size);

	/* Provide the PSP KDB to bootloader */
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_KEY_DATABASE;
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	ret = psp_v13_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v13_0_bootloader_load_sysdrv(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v13_0_is_sos_alive(psp))
		return 0;

	ret = psp_v13_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP System Driver binary to memory */
	memcpy(psp->fw_pri_buf, psp->sys_start_addr, psp->sys_bin_size);

	/* Provide the sys driver to bootloader */
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SYSDRV;
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	ret = psp_v13_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v13_0_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v13_0_is_sos_alive(psp))
		return 0;

	ret = psp_v13_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy Secure OS binary to PSP memory */
	memcpy(psp->fw_pri_buf, psp->sos_start_addr, psp->sos_bin_size);

	/* Provide the PSP secure OS to bootloader */
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SOSDRV;
	WREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMP0_SMN_C2PMSG_81),
			   RREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_81),
			   0, true);

	return ret;
}

static const struct psp_funcs psp_v13_0_funcs = {
	.init_microcode = psp_v13_0_init_microcode,
	.bootloader_load_kdb = psp_v13_0_bootloader_load_kdb,
	.bootloader_load_sysdrv = psp_v13_0_bootloader_load_sysdrv,
	.bootloader_load_sos = psp_v13_0_bootloader_load_sos,
};

void psp_v13_0_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v13_0_funcs;
}
