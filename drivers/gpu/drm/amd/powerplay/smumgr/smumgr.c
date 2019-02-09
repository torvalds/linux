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

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <drm/amdgpu_drm.h>
#include "smumgr.h"

MODULE_FIRMWARE("amdgpu/topaz_smc.bin");
MODULE_FIRMWARE("amdgpu/topaz_k_smc.bin");
MODULE_FIRMWARE("amdgpu/tonga_smc.bin");
MODULE_FIRMWARE("amdgpu/tonga_k_smc.bin");
MODULE_FIRMWARE("amdgpu/fiji_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_smc_sk.bin");
MODULE_FIRMWARE("amdgpu/polaris10_k_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_k2_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_smc_sk.bin");
MODULE_FIRMWARE("amdgpu/polaris11_k_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_k2_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris12_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris12_k_smc.bin");
MODULE_FIRMWARE("amdgpu/vegam_smc.bin");
MODULE_FIRMWARE("amdgpu/vega10_smc.bin");
MODULE_FIRMWARE("amdgpu/vega10_acg_smc.bin");
MODULE_FIRMWARE("amdgpu/vega12_smc.bin");
MODULE_FIRMWARE("amdgpu/vega20_smc.bin");

int smum_thermal_avfs_enable(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->thermal_avfs_enable)
		return hwmgr->smumgr_funcs->thermal_avfs_enable(hwmgr);

	return 0;
}

int smum_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->thermal_setup_fan_table)
		return hwmgr->smumgr_funcs->thermal_setup_fan_table(hwmgr);

	return 0;
}

int smum_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{

	if (NULL != hwmgr->smumgr_funcs->update_sclk_threshold)
		return hwmgr->smumgr_funcs->update_sclk_threshold(hwmgr);

	return 0;
}

int smum_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type)
{

	if (NULL != hwmgr->smumgr_funcs->update_smc_table)
		return hwmgr->smumgr_funcs->update_smc_table(hwmgr, type);

	return 0;
}

uint32_t smum_get_offsetof(struct pp_hwmgr *hwmgr, uint32_t type, uint32_t member)
{
	if (NULL != hwmgr->smumgr_funcs->get_offsetof)
		return hwmgr->smumgr_funcs->get_offsetof(type, member);

	return 0;
}

int smum_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->process_firmware_header)
		return hwmgr->smumgr_funcs->process_firmware_header(hwmgr);
	return 0;
}

uint32_t smum_get_argument(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->get_argument)
		return hwmgr->smumgr_funcs->get_argument(hwmgr);

	return 0;
}

uint32_t smum_get_mac_definition(struct pp_hwmgr *hwmgr, uint32_t value)
{
	if (NULL != hwmgr->smumgr_funcs->get_mac_definition)
		return hwmgr->smumgr_funcs->get_mac_definition(value);

	return 0;
}

int smum_download_powerplay_table(struct pp_hwmgr *hwmgr, void **table)
{
	if (NULL != hwmgr->smumgr_funcs->download_pptable_settings)
		return hwmgr->smumgr_funcs->download_pptable_settings(hwmgr,
									table);
	return 0;
}

int smum_upload_powerplay_table(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->upload_pptable_settings)
		return hwmgr->smumgr_funcs->upload_pptable_settings(hwmgr);

	return 0;
}

int smum_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	if (hwmgr == NULL || hwmgr->smumgr_funcs->send_msg_to_smc == NULL)
		return -EINVAL;

	return hwmgr->smumgr_funcs->send_msg_to_smc(hwmgr, msg);
}

int smum_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
					uint16_t msg, uint32_t parameter)
{
	if (hwmgr == NULL ||
		hwmgr->smumgr_funcs->send_msg_to_smc_with_parameter == NULL)
		return -EINVAL;
	return hwmgr->smumgr_funcs->send_msg_to_smc_with_parameter(
						hwmgr, msg, parameter);
}

int smum_init_smc_table(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->init_smc_table)
		return hwmgr->smumgr_funcs->init_smc_table(hwmgr);

	return 0;
}

int smum_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->populate_all_graphic_levels)
		return hwmgr->smumgr_funcs->populate_all_graphic_levels(hwmgr);

	return 0;
}

int smum_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->populate_all_memory_levels)
		return hwmgr->smumgr_funcs->populate_all_memory_levels(hwmgr);

	return 0;
}

/*this interface is needed by island ci/vi */
int smum_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->initialize_mc_reg_table)
		return hwmgr->smumgr_funcs->initialize_mc_reg_table(hwmgr);

	return 0;
}

bool smum_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->smumgr_funcs->is_dpm_running)
		return hwmgr->smumgr_funcs->is_dpm_running(hwmgr);

	return true;
}

bool smum_is_hw_avfs_present(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->smumgr_funcs->is_hw_avfs_present)
		return hwmgr->smumgr_funcs->is_hw_avfs_present(hwmgr);

	return false;
}

int smum_update_dpm_settings(struct pp_hwmgr *hwmgr, void *profile_setting)
{
	if (hwmgr->smumgr_funcs->update_dpm_settings)
		return hwmgr->smumgr_funcs->update_dpm_settings(hwmgr, profile_setting);

	return -EINVAL;
}

int smum_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table, uint16_t table_id, bool rw)
{
	if (hwmgr->smumgr_funcs->smc_table_manager)
		return hwmgr->smumgr_funcs->smc_table_manager(hwmgr, table, table_id, rw);

	return -EINVAL;
}
