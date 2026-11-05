// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#include "ras_eeprom_mgr.h"
#include "ras.h"
#include "eeprom.h"
#include "eeprom_fw.h"

static bool __ras_eeprom_disabled(struct ras_core_context *ras_core)
{
	return !ras_core->ras_eeprom_supported;
}

static const struct ras_eeprom_ops *ras_eeprom_mgr_get_ip_func(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;

	switch (ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
	case IP_VERSION(13, 0, 15):
		mgr->eeprom_early_init_service_supported = false;
		return &ras_drv_eeprom_ops;
	case IP_VERSION(15, 0, 8):
		mgr->eeprom_early_init_service_supported = true;
		mgr->fw_record_enabled = true;
		return &ras_fw_eeprom_ops;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"EEPROM(MP1) ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_eeprom_mgr_sw_init(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct ras_eeprom_config *cfg;
	struct ras_eeprom_param_config  param_cfg = {0};
	struct ras_eeprom_param eeprom_param = {0};
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	cfg = &ras_core->config->eeprom_cfg;
	if (!cfg || !cfg->eeprom_sys_fn || !cfg->eeprom_sys_fn->get_eeprom_config) {
		RAS_DEV_ERR(ras_core->dev, "Ras eeprom not configured!\n");
		return -EINVAL;
	}

	memset(mgr, 0, sizeof(*mgr));

	mgr->sys_func = cfg->eeprom_sys_fn;
	ret = mgr->sys_func->get_eeprom_config(ras_core, &param_cfg);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to get ras eeprom param config!\n");
		return -EPERM;
	}

	mgr->ras_err_threshold = param_cfg.eeprom_record_threshold_count;
	mgr->work_mode_over_thresh = param_cfg.work_mode_over_thresh;
	mgr->eeprom_ip_version = param_cfg.eeprom_ip_version;
	mgr->eeprom_ops = ras_eeprom_mgr_get_ip_func(ras_core, mgr->eeprom_ip_version);
	if (!mgr->eeprom_ops)
		return -ENODEV;

	eeprom_param.sys_fn = mgr->sys_func;
	eeprom_param.eeprom_ip_version = param_cfg.eeprom_ip_version;
	eeprom_param.record_threshold = param_cfg.eeprom_record_threshold_count;
	eeprom_param.max_i2c_read_len = param_cfg.max_i2c_read_len;
	eeprom_param.max_i2c_write_len = param_cfg.max_i2c_write_len;
	eeprom_param.eeprom_i2c_adapter = param_cfg.eeprom_i2c_adapter;
	eeprom_param.eeprom_i2c_port = param_cfg.eeprom_i2c_port;
	eeprom_param.eeprom_i2c_addr = param_cfg.eeprom_i2c_addr;

	if (mgr->eeprom_ops->sw_init)
		ret = mgr->eeprom_ops->sw_init(ras_core, &eeprom_param);

	return ret;
}

int ras_eeprom_mgr_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops->sw_fini)
		ret = mgr->eeprom_ops->sw_fini(ras_core);

	return ret;
}

int ras_eeprom_mgr_hw_init(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct ras_eeprom_param_config  param_cfg = {0};
	struct ras_eeprom_param eeprom_param = {0};
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	ret = mgr->sys_func->get_eeprom_config(ras_core, &param_cfg);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to get ras eeprom param config!\n");
		return -EPERM;
	}

	eeprom_param.max_i2c_read_len = param_cfg.max_i2c_read_len;
	eeprom_param.max_i2c_write_len = param_cfg.max_i2c_write_len;
	eeprom_param.eeprom_i2c_adapter = param_cfg.eeprom_i2c_adapter;
	eeprom_param.eeprom_i2c_port = param_cfg.eeprom_i2c_port;
	eeprom_param.eeprom_i2c_addr = param_cfg.eeprom_i2c_addr;

	if (mgr->eeprom_ops->hw_init)
		ret = mgr->eeprom_ops->hw_init(ras_core, &eeprom_param);

	return ret;
}

int ras_eeprom_mgr_hw_fini(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops->hw_fini)
		ret = mgr->eeprom_ops->hw_fini(ras_core);

	return ret;
}

int ras_eeprom_mgr_reset_table(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = -EOPNOTSUPP;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops->reset_table)
		ret = mgr->eeprom_ops->reset_table(ras_core);

	return ret;
}

int ras_eeprom_mgr_get_record_count(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops && mgr->eeprom_ops->get_record_count)
		ret = mgr->eeprom_ops->get_record_count(ras_core);

	return ret;
}

int ras_eeprom_mgr_get_records(struct ras_core_context *ras_core, u32 start,
		struct eeprom_umc_record *record, u32 num)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = -EOPNOTSUPP;

	if (__ras_eeprom_disabled(ras_core))
		return -RAS_CORE_NOT_SUPPORTED;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops->get_records)
		ret = mgr->eeprom_ops->get_records(ras_core,
					start, record, num);

	return ret;
}

int ras_eeprom_mgr_append_records(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record, const u32 num)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	int ret = -EOPNOTSUPP;

	if (__ras_eeprom_disabled(ras_core))
		return -RAS_CORE_NOT_SUPPORTED;

	if (!mgr->eeprom_ops)
		return -EINVAL;

	if (mgr->eeprom_ops->append_records)
		ret = mgr->eeprom_ops->append_records(ras_core,
					record, num);

	return ret;
}

int ras_eeprom_mgr_get_eeprom_info(struct ras_core_context *ras_core,
			struct ras_eeprom_info *eeprom_info)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;

	if (__ras_eeprom_disabled(ras_core))
		return -EOPNOTSUPP;

	if (!mgr->eeprom_ops || !mgr->eeprom_ops->get_eeprom_info)
		return -EOPNOTSUPP;

	return mgr->eeprom_ops->get_eeprom_info(ras_core,
				eeprom_info, ras_core_gpu_in_reset(ras_core));
}

int ras_eeprom_mgr_check_and_report_status(struct ras_core_context *ras_core, bool chk_hw)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct ras_eeprom_info info = {0};
	int valid_err_count;
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops || !mgr->eeprom_ops->get_eeprom_info)
		return -EOPNOTSUPP;

	ret = mgr->eeprom_ops->get_eeprom_info(ras_core, &info, !chk_hw);
	if (ret)
		return ret;

	valid_err_count = ras_umc_get_saved_eeprom_count(ras_core);
	if (info.eeprom_status == RAS_EEPROM_OK) {
		RAS_DEV_DBG(ras_core->dev,
			"Found existing EEPROM table with %d records\n",
			valid_err_count);

		/* Warn if error count reaches or exceeds 90% of the threshold */
		if (10 * valid_err_count >= 9 * mgr->ras_err_threshold)
			RAS_DEV_WARN(ras_core->dev,
				"RAS records:%u exceeds 90%% of threshold:%d\n",
				valid_err_count, mgr->ras_err_threshold);

	} else if (info.eeprom_status == RAS_EEPROM_LOCKED) {
		if (info.rma_status || (valid_err_count >= mgr->ras_err_threshold)) {
			if (info.rma_status)
				RAS_DEV_ERR(ras_core->dev, "RAS records:%d exceed error threshold",
					valid_err_count);
			else
				RAS_DEV_ERR(ras_core->dev, "RAS records:%d exceed threshold:%d",
					valid_err_count, mgr->ras_err_threshold);

			if (mgr->work_mode_over_thresh == RAS_WORK_MODE_OVER_THRESH_STRICT) {
				RAS_DEV_WARN(ras_core->dev,
				"Please consult AMD Service Action Guide (SAG) for appropriate service procedures\n");
			} else if (mgr->work_mode_over_thresh == RAS_WORK_MODE_OVER_THRESH_RMA) {
				ras_core->is_rma = true;
				RAS_DEV_ERR(ras_core->dev,
				"User defined threshold is set, runtime service will be halt when threshold is reached\n");
			}
		} else if ((valid_err_count < mgr->ras_err_threshold) &&
				mgr->eeprom_ops->unlock_eeprom) {
			/* This means that, the threshold was increased since
			 * the last time the system was booted, and now,
			 * the saved error count is less than threshold,
			 * so that at least one more record can be saved,
			 * before the page count threshold is reached.
			 */
			ret = mgr->eeprom_ops->unlock_eeprom(ras_core);
			if (ret) {
				RAS_DEV_ERR(ras_core->dev, "Failed to resize RAS table\n");
			} else {
				ras_core->is_rma = false;
				RAS_DEV_INFO(ras_core->dev,
					"records:%d threshold:%d, resetting RAS table header signature",
					valid_err_count, mgr->ras_err_threshold);
			}
		} else {
			RAS_DEV_ERR(ras_core->dev,
				"records:%d threshold:%d, not support to resize RAS table\n",
				valid_err_count, mgr->ras_err_threshold);
		}
	} else {
		RAS_DEV_ERR(ras_core->dev, "Error ras eeprom status:0x%x\n", info.eeprom_status);
	}

	return ret;
}

enum ras_gpu_op_status
	ras_eeprom_mgr_get_gpu_op_status(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct ras_eeprom_info info = {0};
	int ret;

	if (__ras_eeprom_disabled(ras_core))
		return RAS_GPU_OP_STATUS_OK;

	if (!mgr->eeprom_ops || !mgr->eeprom_ops->get_eeprom_info)
		return RAS_GPU_OP_STATUS_UNKNOWN;

	ret = mgr->eeprom_ops->get_eeprom_info(ras_core,
				&info, ras_core_gpu_in_reset(ras_core));
	if (ret)
		return RAS_GPU_OP_STATUS_UNKNOWN;

	if (info.rma_status || ras_core->is_rma)
		return RAS_GPU_OP_STATUS_RMA;

	if (info.eeprom_status == RAS_EEPROM_FAULT)
		return RAS_GPU_OP_STATUS_FAULT;
	else if (info.eeprom_status == RAS_EEPROM_LOCKED)
		return RAS_GPU_OP_STATUS_LOCKED;
	else if (info.eeprom_status == RAS_EEPROM_UNKNOWN)
		return RAS_GPU_OP_STATUS_UNKNOWN;

	return RAS_GPU_OP_STATUS_OK;
}

bool ras_eeprom_mgr_check_safety_watermark(struct ras_core_context *ras_core)
{
	enum ras_gpu_op_status status;

	if (__ras_eeprom_disabled(ras_core))
		return true;

	ras_eeprom_mgr_check_and_report_status(ras_core, false);

	status = ras_eeprom_mgr_get_gpu_op_status(ras_core);

	return (status == RAS_GPU_OP_STATUS_OK);
}

int ras_eeprom_mgr_get_version(struct ras_core_context *ras_core, u32 *version)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct ras_eeprom_info info = {0};
	int ret = 0;

	if (__ras_eeprom_disabled(ras_core))
		return 0;

	if (!mgr->eeprom_ops || !mgr->eeprom_ops->get_eeprom_info)
		return -EOPNOTSUPP;

	ret = mgr->eeprom_ops->get_eeprom_info(ras_core, &info, true);
	if (ret)
		return ret;

	*version = info.record_format_version;

	return 0;
}

bool ras_eeprom_mgr_early_init_service_supported(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;

	if (__ras_eeprom_disabled(ras_core))
		return false;

	return mgr->eeprom_early_init_service_supported;
}

bool ras_eeprom_mgr_fw_record_enabled(struct ras_core_context *ras_core)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;

	if (__ras_eeprom_disabled(ras_core))
		return false;

	return mgr->fw_record_enabled;
}
