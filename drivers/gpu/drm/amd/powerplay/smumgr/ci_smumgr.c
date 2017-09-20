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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include "linux/delay.h"

#include "smumgr.h"
#include "ci_smumgr.h"
#include "cgs_common.h"
#include "ci_smc.h"

static int ci_smu_init(struct pp_hwmgr *hwmgr)
{
	int i;
	struct ci_smumgr *ci_priv = NULL;

	ci_priv = kzalloc(sizeof(struct ci_smumgr), GFP_KERNEL);

	if (ci_priv == NULL)
		return -ENOMEM;

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++)
		ci_priv->activity_target[i] = 30;

	hwmgr->smumgr->backend = ci_priv;

	return 0;
}

static int ci_smu_fini(struct pp_hwmgr *hwmgr)
{
	kfree(hwmgr->smumgr->backend);
	hwmgr->smumgr->backend = NULL;
	cgs_rel_firmware(hwmgr->device, CGS_UCODE_ID_SMU);
	return 0;
}

static int ci_start_smu(struct pp_hwmgr *hwmgr)
{
	return 0;
}

const struct pp_smumgr_func ci_smu_funcs = {
	.smu_init = ci_smu_init,
	.smu_fini = ci_smu_fini,
	.start_smu = ci_start_smu,
	.check_fw_load_finish = NULL,
	.request_smu_load_fw = NULL,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = ci_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = ci_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.get_offsetof = ci_get_offsetof,
	.process_firmware_header = ci_process_firmware_header,
	.init_smc_table = ci_init_smc_table,
	.update_sclk_threshold = ci_update_sclk_threshold,
	.thermal_setup_fan_table = ci_thermal_setup_fan_table,
	.populate_all_graphic_levels = ci_populate_all_graphic_levels,
	.populate_all_memory_levels = ci_populate_all_memory_levels,
	.get_mac_definition = ci_get_mac_definition,
	.initialize_mc_reg_table = ci_initialize_mc_reg_table,
	.is_dpm_running = ci_is_dpm_running,
	.populate_requested_graphic_levels = ci_populate_requested_graphic_levels,
};
