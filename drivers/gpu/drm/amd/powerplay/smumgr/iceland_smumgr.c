/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "iceland_smumgr.h"
#include "pp_debug.h"
#include "smu_ucode_xfer_vi.h"
#include "ppsmc.h"
#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"
#include "cgs_common.h"
#include "iceland_smc.h"

#define ICELAND_SMC_SIZE               0x20000

static int iceland_start_smc(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	return 0;
}

static void iceland_reset_smc(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL,
				  rst_reg, 1);
}


static void iceland_stop_smc_clock(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 1);
}

static void iceland_start_smc_clock(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 0);
}

static int iceland_smu_start_smc(struct pp_smumgr *smumgr)
{
	/* set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(smumgr);

	/* enable smc clock */
	iceland_start_smc_clock(smumgr);

	/* de-assert reset */
	iceland_start_smc(smumgr);

	SMUM_WAIT_INDIRECT_FIELD(smumgr, SMC_IND, FIRMWARE_FLAGS,
				 INTERRUPTS_ENABLED, 1);

	return 0;
}


static int iceland_upload_smc_firmware_data(struct pp_smumgr *smumgr,
					uint32_t length, const uint8_t *src,
					uint32_t limit, uint32_t start_addr)
{
	uint32_t byte_count = length;
	uint32_t data;

	PP_ASSERT_WITH_CODE((limit >= byte_count), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_0, start_addr);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 1);

	while (byte_count >= 4) {
		data = src[0] * 0x1000000 + src[1] * 0x10000 + src[2] * 0x100 + src[3];
		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data);
		src += 4;
		byte_count -= 4;
	}

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);

	PP_ASSERT_WITH_CODE((0 == byte_count), "SMC size must be dividable by 4.", return -EINVAL);

	return 0;
}


static int iceland_smu_upload_firmware_image(struct pp_smumgr *smumgr)
{
	uint32_t val;
	struct cgs_firmware_info info = {0};

	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	/* load SMC firmware */
	cgs_get_firmware_info(smumgr->device,
		smu7_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);

	if (info.image_size & 3) {
		pr_err("[ powerplay ] SMC ucode is not 4 bytes aligned\n");
		return -EINVAL;
	}

	if (info.image_size > ICELAND_SMC_SIZE) {
		pr_err("[ powerplay ] SMC address is beyond the SMC RAM area\n");
		return -EINVAL;
	}

	/* wait for smc boot up */
	SMUM_WAIT_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
					 RCU_UC_EVENTS, boot_seq_done, 0);

	/* clear firmware interrupt enable flag */
	val = cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC,
				    ixSMC_SYSCON_MISC_CNTL);
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
			       ixSMC_SYSCON_MISC_CNTL, val | 1);

	/* stop smc clock */
	iceland_stop_smc_clock(smumgr);

	/* reset smc */
	iceland_reset_smc(smumgr);
	iceland_upload_smc_firmware_data(smumgr, info.image_size,
				(uint8_t *)info.kptr, ICELAND_SMC_SIZE,
				info.ucode_start_address);

	return 0;
}

static int iceland_request_smu_load_specific_fw(struct pp_smumgr *smumgr,
						uint32_t firmwareType)
{
	return 0;
}

static int iceland_start_smu(struct pp_smumgr *smumgr)
{
	int result;

	result = iceland_smu_upload_firmware_image(smumgr);
	if (result)
		return result;
	result = iceland_smu_start_smc(smumgr);
	if (result)
		return result;

	if (!smu7_is_smc_ram_running(smumgr)) {
		printk("smu not running, upload firmware again \n");
		result = iceland_smu_upload_firmware_image(smumgr);
		if (result)
			return result;

		result = iceland_smu_start_smc(smumgr);
		if (result)
			return result;
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
static int iceland_smu_init(struct pp_smumgr *smumgr)
{
	int i;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(smumgr->backend);
	if (smu7_init(smumgr))
		return -EINVAL;

	for (i = 0; i < SMU71_MAX_LEVELS_GRAPHICS; i++)
		smu_data->activity_target[i] = 30;

	return 0;
}

static const struct pp_smumgr_func iceland_smu_funcs = {
	.smu_init = &iceland_smu_init,
	.smu_fini = &smu7_smu_fini,
	.start_smu = &iceland_start_smu,
	.check_fw_load_finish = &smu7_check_fw_load_finish,
	.request_smu_load_fw = &smu7_reload_firmware,
	.request_smu_load_specific_fw = &iceland_request_smu_load_specific_fw,
	.send_msg_to_smc = &smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu7_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.get_offsetof = iceland_get_offsetof,
	.process_firmware_header = iceland_process_firmware_header,
	.init_smc_table = iceland_init_smc_table,
	.update_sclk_threshold = iceland_update_sclk_threshold,
	.thermal_setup_fan_table = iceland_thermal_setup_fan_table,
	.populate_all_graphic_levels = iceland_populate_all_graphic_levels,
	.populate_all_memory_levels = iceland_populate_all_memory_levels,
	.get_mac_definition = iceland_get_mac_definition,
	.initialize_mc_reg_table = iceland_initialize_mc_reg_table,
	.is_dpm_running = iceland_is_dpm_running,
};

int iceland_smum_init(struct pp_smumgr *smumgr)
{
	struct iceland_smumgr *iceland_smu = NULL;

	iceland_smu = kzalloc(sizeof(struct iceland_smumgr), GFP_KERNEL);

	if (iceland_smu == NULL)
		return -ENOMEM;

	smumgr->backend = iceland_smu;
	smumgr->smumgr_funcs = &iceland_smu_funcs;

	return 0;
}
