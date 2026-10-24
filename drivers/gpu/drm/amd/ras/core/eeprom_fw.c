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

#include "ras.h"
#include "ras_mp1.h"
#include "eeprom_fw.h"
#define MAX_EEPROM_ERR_RECORD_NUM 1024

static int fw_eeprom_reset_ras_table(struct ras_core_context *ras_core)
{
	struct fw_eeprom_control *ctl = ras_core->eeprom_mgr.ras_eeprom;
	u32 erase_res = 0;
	int res;

	mutex_lock(&ctl->record_lock);
	res = ras_mp1_reset_ras_table(ras_core, &erase_res);
	if (res || erase_res) {
		RAS_DEV_WARN(ras_core->dev,
			"RAS EEPROM reset failed, res:%d result:%d\n", res, erase_res);
		if (!res)
			res = -EIO;
		goto out;
	}

	ctl->record_count = 0;
	memset(ctl->records, 0,
		sizeof(*ctl->records) * MAX_EEPROM_ERR_RECORD_NUM);

	ctl->ras_table_format_version = 0;
	ctl->eeprom_status = 0;
	ctl->rma_status = 0;
	ctl->bad_channel_bitmap = 0;

out:
	mutex_unlock(&ctl->record_lock);
	return res;
}

static int fw_eeprom_sync_data(struct ras_core_context *ras_core,
		struct fw_eeprom_control *ctl)
{
	struct eeprom_err_record err_rec = {0};
	u32 fw_err_rec_num;
	u32 idx;
	int ret = 0;

	mutex_lock(&ctl->record_lock);
	ret = ras_mp1_get_record_count(ras_core, &fw_err_rec_num);
	if (ret)
		goto out;

	if (!fw_err_rec_num || fw_err_rec_num == ctl->record_count) {
		goto out;
	} else if (fw_err_rec_num < ctl->record_count) {
		RAS_DEV_ERR(ras_core->dev, "EEPROM ECC error count mismatch!\n");
		ret = -EFAULT;
		goto out;
	} else if (fw_err_rec_num > MAX_EEPROM_ERR_RECORD_NUM) {
		RAS_DEV_ERR(ras_core->dev,
			"Invalid EEPROM error count:0x%x\n", fw_err_rec_num);
		ret = -EOVERFLOW;
		goto out;
	}

	for (idx = ctl->record_count;
			idx < fw_err_rec_num; idx++, ctl->record_count = idx) {
		ret = ras_mp1_get_record(ras_core, idx, &err_rec);
		if (ret)
			goto out;

		memcpy(&ctl->records[idx], &err_rec, sizeof(*ctl->records));
	}

out:
	mutex_unlock(&ctl->record_lock);
	return ret;
}

static int fw_eeprom_get_record_count(struct ras_core_context *ras_core)
{
	struct fw_eeprom_control *ctl;

	if (!ras_core->eeprom_mgr.ras_eeprom)
		return -EINVAL;

	ctl = ras_core->eeprom_mgr.ras_eeprom;

	if (!ras_core_gpu_in_reset(ras_core))
		fw_eeprom_sync_data(ras_core, ctl);

	return ctl->record_count;
}

static int fw_eeprom_get_record(struct ras_core_context *ras_core,
		u32 idx, struct eeprom_umc_record *rec)
{
	struct fw_eeprom_control *ctl;
	struct ras_bank_ecc bank = {0};

	if (!ras_core->eeprom_mgr.ras_eeprom)
		return -EINVAL;

	ctl = ras_core->eeprom_mgr.ras_eeprom;

	if (!rec || idx >= ctl->record_count)
		return -EINVAL;

	bank.timestamp = ctl->records[idx].timestamp;
	bank.status = 0;
	bank.ipid = ctl->records[idx].ipid;
	bank.addr = ctl->records[idx].mca_addr;
	bank.nps = ras_core_get_curr_nps_mode(ras_core);

	return ras_umc_bank_to_umc_record(ras_core, &bank, rec);
}

static int fw_eeprom_sw_init(struct ras_core_context *ras_core,
			struct ras_eeprom_param *param)
{
	struct ras_eeprom_mgr *mgr = &ras_core->eeprom_mgr;
	struct fw_eeprom_control *ctl;
	int ret = 0;

	if (!param)
		return -EINVAL;

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	ctl->eeprom_ip_version = param->eeprom_ip_version;
	ctl->records = kzalloc(sizeof(*ctl->records) * MAX_EEPROM_ERR_RECORD_NUM, GFP_KERNEL);
	if (!ctl->records) {
		ret = -ENOMEM;
		goto out;
	}

	ctl->max_record_count = MAX_EEPROM_ERR_RECORD_NUM;
	mutex_init(&ctl->record_lock);

	mgr->ras_eeprom = ctl;

	return 0;

out:
	kfree(ctl);
	return ret;
}

static int fw_eeprom_sw_fini(struct ras_core_context *ras_core)
{
	struct fw_eeprom_control *ctl;

	if (!ras_core->eeprom_mgr.ras_eeprom)
		return -EINVAL;

	ctl = ras_core->eeprom_mgr.ras_eeprom;

	kfree(ctl->records);
	mutex_destroy(&ctl->record_lock);

	kfree(ctl);
	ras_core->eeprom_mgr.ras_eeprom = NULL;

	return 0;
}

static int fw_eeprom_hw_init(struct ras_core_context *ras_core,
			struct ras_eeprom_param *param)
{
	struct fw_eeprom_control *ctl;

	if (!ras_core->eeprom_mgr.ras_eeprom)
		return -EINVAL;

	ctl = ras_core->eeprom_mgr.ras_eeprom;

	ras_mp1_get_table_version(ras_core, &ctl->ras_table_format_version);

	return 0;
}

static int fw_eeprom_hw_fini(struct ras_core_context *ras_core)
{
	return 0;
}

static int fw_eeprom_get_records(struct ras_core_context *ras_core, u32 start,
		struct eeprom_umc_record *record, u32 num)
{
	struct fw_eeprom_control *ctl = ras_core->eeprom_mgr.ras_eeprom;
	int i, ret = 0;

	mutex_lock(&ctl->record_lock);
	for (i = 0; i < num; i++) {
		ret = fw_eeprom_get_record(ras_core, start + i, &record[i]);
		if (ret)
			break;
	}
	mutex_unlock(&ctl->record_lock);

	return ret;
}

static int fw_eeprom_get_eeprom_info(struct ras_core_context *ras_core,
		struct ras_eeprom_info *eeprom_info, bool fast_mode)
{
	struct fw_eeprom_control *ctl = ras_core->eeprom_mgr.ras_eeprom;

	if (!eeprom_info)
		return -EINVAL;

	eeprom_info->record_count = ctl->record_count;
	eeprom_info->max_record_count = ctl->max_record_count;
	eeprom_info->bad_channel_bitmap = ctl->bad_channel_bitmap;

	if (!fast_mode) {
		if (!ctl->ras_table_format_version)
			ras_mp1_get_table_version(ras_core,
				&ctl->ras_table_format_version);

		eeprom_info->rma_status = ras_mp1_rma_detected(ras_core);
		ctl->rma_status = eeprom_info->rma_status;
	} else {
		eeprom_info->rma_status = ctl->rma_status;
	}

	eeprom_info->record_format_version = ctl->ras_table_format_version;

	if (eeprom_info->rma_status)
		eeprom_info->eeprom_status = RAS_EEPROM_LOCKED;
	else
		eeprom_info->eeprom_status = RAS_EEPROM_OK;

	return 0;
}

struct ras_eeprom_ops ras_fw_eeprom_ops = {
	.sw_init = fw_eeprom_sw_init,
	.sw_fini = fw_eeprom_sw_fini,
	.hw_init = fw_eeprom_hw_init,
	.hw_fini = fw_eeprom_hw_fini,
	.reset_table = fw_eeprom_reset_ras_table,
	.get_records = fw_eeprom_get_records,
	.get_record_count = fw_eeprom_get_record_count,
	.get_eeprom_info = fw_eeprom_get_eeprom_info,
};
