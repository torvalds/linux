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
#include "pp_debug.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "tonga_smumgr.h"
#include "smu_ucode_xfer_vi.h"
#include "tonga_ppsmc.h"
#include "smu/smu_7_1_2_d.h"
#include "smu/smu_7_1_2_sh_mask.h"
#include "cgs_common.h"
#include "tonga_smc.h"
#include "smu7_smumgr.h"


static int tonga_start_in_protection_mode(struct pp_smumgr *smumgr)
{
	int result;

	/* Assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);
	if (result)
		return result;

	/* Clear status */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixSMU_STATUS, 0);

	/* Enable clock */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Set SMU Auto Start */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMU_INPUT_DATA, AUTO_START, 1);

	/* Clear firmware interrupt enable flag */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);

	/**
	 * Call Test SMU message with 0x20000 offset to trigger SMU start
	 */
	smu7_send_msg_to_smc_offset(smumgr);

	/* Wait for done bit to be set */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
		SMU_STATUS, SMU_DONE, 0);

	/* Check pass/failed indicator */
	if (1 != SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device,
				CGS_IND_REG__SMC, SMU_STATUS, SMU_PASS)) {
		pr_err("SMU Firmware start failed\n");
		return -EINVAL;
	}

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return 0;
}


static int tonga_start_in_non_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* wait for smc boot up */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
		RCU_UC_EVENTS, boot_seq_done, 0);

	/*Clear firmware interrupt enable flag*/
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);


	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);

	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(smumgr);


	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/*De-assert reset*/
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int tonga_start_smu(struct pp_smumgr *smumgr)
{
	int result;

	/* Only start SMC if SMC RAM is not running */
	if (!(smu7_is_smc_ram_running(smumgr) ||
		cgs_is_virtualization_enabled(smumgr->device))) {
		/*Check if SMU is running in protected mode*/
		if (0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMU_FIRMWARE, SMU_MODE)) {
			result = tonga_start_in_non_protection_mode(smumgr);
			if (result)
				return result;
		} else {
			result = tonga_start_in_protection_mode(smumgr);
			if (result)
				return result;
		}
	}

	result = smu7_request_smu_load_fw(smumgr);

	return result;
}

/**
 * Write a 32bit value to the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value to write to the SMC SRAM.
 */
static int tonga_smu_init(struct pp_smumgr *smumgr)
{
	struct tonga_smumgr *tonga_priv = NULL;
	int  i;

	tonga_priv = kzalloc(sizeof(struct tonga_smumgr), GFP_KERNEL);
	if (tonga_priv == NULL)
		return -ENOMEM;

	smumgr->backend = tonga_priv;

	if (smu7_init(smumgr))
		return -EINVAL;

	for (i = 0; i < SMU72_MAX_LEVELS_GRAPHICS; i++)
		tonga_priv->activity_target[i] = 30;

	return 0;
}

const struct pp_smumgr_func tonga_smu_funcs = {
	.smu_init = &tonga_smu_init,
	.smu_fini = &smu7_smu_fini,
	.start_smu = &tonga_start_smu,
	.check_fw_load_finish = &smu7_check_fw_load_finish,
	.request_smu_load_fw = &smu7_request_smu_load_fw,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu7_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.update_smc_table = tonga_update_smc_table,
	.get_offsetof = tonga_get_offsetof,
	.process_firmware_header = tonga_process_firmware_header,
	.init_smc_table = tonga_init_smc_table,
	.update_sclk_threshold = tonga_update_sclk_threshold,
	.thermal_setup_fan_table = tonga_thermal_setup_fan_table,
	.populate_all_graphic_levels = tonga_populate_all_graphic_levels,
	.populate_all_memory_levels = tonga_populate_all_memory_levels,
	.get_mac_definition = tonga_get_mac_definition,
	.initialize_mc_reg_table = tonga_initialize_mc_reg_table,
	.is_dpm_running = tonga_is_dpm_running,
};
