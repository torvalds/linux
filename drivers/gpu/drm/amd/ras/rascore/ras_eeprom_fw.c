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

#define RAS_SMU_MESSAGE_TIMEOUT_MS 1000 /* 1s */

void ras_fw_init_feature_flags(struct ras_core_context *ras_core)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint64_t flags = 0ULL;

	if (!sys_func || !sys_func->mp1_get_ras_enabled_mask)
		return;

	if (!sys_func->mp1_get_ras_enabled_mask(ras_core, &flags))
		ras_core->ras_fw_features = flags;
}

bool ras_fw_eeprom_supported(struct ras_core_context *ras_core)
{
	return !!(ras_core->ras_fw_features & RAS_CORE_FW_FEATURE_BIT__RAS_EEPROM);
}

int ras_fw_get_table_version(struct ras_core_context *ras_core,
				     uint32_t *table_version)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;

	return sys_func->mp1_send_eeprom_msg(ras_core,
				RAS_SMU_GetRASTableVersion, 0, table_version);
}

int ras_fw_get_badpage_count(struct ras_core_context *ras_core,
				     uint32_t *count, uint32_t timeout)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint64_t end, now;
	int ret = 0;

	now = (uint64_t)ktime_to_ms(ktime_get());
	end = now + timeout;

	do {
		ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetBadPageCount, 0, count);
		/* eeprom is not ready */
		if (ret != -EBUSY)
			return ret;

		mdelay(10);
		now = (uint64_t)ktime_to_ms(ktime_get());
	} while (now < end);

	RAS_DEV_ERR(ras_core->dev,
			"smu get bad page count timeout!\n");
	return ret;
}

int ras_fw_get_badpage_mca_addr(struct ras_core_context *ras_core,
					uint16_t index, uint64_t *mca_addr)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint32_t temp_arg, temp_addr_lo, temp_addr_high;
	int ret;

	temp_arg = index | (1 << 16);
	ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetBadPageMcaAddr, temp_arg, &temp_addr_lo);
	if (ret)
		return ret;

	temp_arg = index | (2 << 16);
	ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetBadPageMcaAddr, temp_arg, &temp_addr_high);

	if (!ret)
		*mca_addr = (uint64_t)temp_addr_high << 32 | temp_addr_lo;

	return ret;
}

int ras_fw_set_timestamp(struct ras_core_context *ras_core,
				 uint64_t timestamp)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;

	return sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_SetTimestamp, (uint32_t)timestamp, 0);
}

int ras_fw_get_timestamp(struct ras_core_context *ras_core,
				 uint16_t index, uint64_t *timestamp)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint32_t temp = 0;
	int ret;

	ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetTimestamp, index, &temp);
	if (!ret)
		*timestamp = temp;

	return ret;
}

int ras_fw_get_badpage_ipid(struct ras_core_context *ras_core,
				    uint16_t index, uint64_t *ipid)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint32_t temp_arg, temp_ipid_lo, temp_ipid_high;
	int ret;

	temp_arg = index | (1 << 16);
	ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetBadPageIpid, temp_arg, &temp_ipid_lo);
	if (ret)
		return ret;

	temp_arg = index | (2 << 16);
	ret = sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_GetBadPageIpid, temp_arg, &temp_ipid_high);
	if (!ret)
		*ipid = (uint64_t)temp_ipid_high << 32 | temp_ipid_lo;

	return ret;
}

int ras_fw_erase_ras_table(struct ras_core_context *ras_core,
				   uint32_t *result)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;

	return sys_func->mp1_send_eeprom_msg(ras_core,
			RAS_SMU_EraseRasTable, 0, result);
}

int ras_fw_eeprom_reset_table(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	u32 erase_res = 0;
	int res;

	mutex_lock(&control->ras_tbl_mutex);

	res = ras_fw_erase_ras_table(ras_core, &erase_res);
	if (res || erase_res) {
		RAS_DEV_WARN(ras_core->dev, "RAS EEPROM reset failed, res:%d result:%d",
									res, erase_res);
		if (!res)
			res = -EIO;
	}

	control->ras_num_recs = 0;
	control->bad_channel_bitmap = 0;
	ras_core_event_notify(ras_core, RAS_EVENT_ID__UPDATE_BAD_PAGE_NUM,
		&control->ras_num_recs);
	ras_core_event_notify(ras_core, RAS_EVENT_ID__UPDATE_BAD_CHANNEL_BITMAP,
		&control->bad_channel_bitmap);
	control->update_channel_flag = false;

	mutex_unlock(&control->ras_tbl_mutex);

	return res;
}

bool ras_fw_eeprom_check_safety_watermark(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	bool ret = false;
	int bad_page_count;

	if (!control->record_threshold_config)
		return false;

	bad_page_count = ras_umc_get_badpage_count(ras_core);

	if (bad_page_count > control->record_threshold_count)
		RAS_DEV_WARN(ras_core->dev, "RAS records:%d exceed threshold:%d",
			bad_page_count, control->record_threshold_count);

	if ((control->record_threshold_config == WARN_NONSTOP_OVER_THRESHOLD) ||
		(control->record_threshold_config == NONSTOP_OVER_THRESHOLD)) {
		RAS_DEV_WARN(ras_core->dev,
			"Please consult AMD Service Action Guide (SAG) for appropriate service procedures.\n");
		ret = false;
	} else {
		ras_core->is_rma = true;
		RAS_DEV_WARN(ras_core->dev,
			"Please consider adjusting the customized threshold.\n");
		ret = true;
	}

	return ret;
}

int ras_fw_eeprom_append(struct ras_core_context *ras_core,
			   struct eeprom_umc_record *record, const u32 num)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	int threshold_config = control->record_threshold_config;
	int i, bad_page_count;

	mutex_lock(&control->ras_tbl_mutex);

	for (i = 0; i < num; i++) {
		/* update bad channel bitmap */
		if ((record[i].mem_channel < BITS_PER_TYPE(control->bad_channel_bitmap)) &&
			!(control->bad_channel_bitmap & (1 << record[i].mem_channel))) {
			control->bad_channel_bitmap |= 1 << record[i].mem_channel;
			control->update_channel_flag = true;
		}
	}
	control->ras_num_recs += num;

	bad_page_count = ras_umc_get_badpage_count(ras_core);

	if (threshold_config != 0 &&
		bad_page_count > control->record_threshold_count) {
		RAS_DEV_WARN(ras_core->dev,
			"Saved bad pages %d reaches threshold value %d\n",
			bad_page_count, control->record_threshold_count);

		if ((threshold_config != WARN_NONSTOP_OVER_THRESHOLD) &&
			(threshold_config != NONSTOP_OVER_THRESHOLD))
			ras_core->is_rma = true;

		/* ignore the -ENOTSUPP return value */
		ras_core_event_notify(ras_core, RAS_EVENT_ID__DEVICE_RMA, NULL);
	}

	mutex_unlock(&control->ras_tbl_mutex);
	return 0;
}

int ras_fw_eeprom_read_idx(struct ras_core_context *ras_core,
			 struct eeprom_umc_record *record_umc,
			 struct ras_bank_ecc *ras_ecc,
			 u32 rec_idx, const u32 num)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	int i, ret, end_idx;
	u64 mca, ipid, ts;

	if (!ras_core->ras_umc.ip_func ||
	    !ras_core->ras_umc.ip_func->mca_ipid_parse)
		return -EOPNOTSUPP;

	mutex_lock(&control->ras_tbl_mutex);

	end_idx = rec_idx + num;
	for (i = rec_idx; i < end_idx; i++) {
		ret = ras_fw_get_badpage_mca_addr(ras_core, i, &mca);
		if (ret)
			goto out;

		ret = ras_fw_get_badpage_ipid(ras_core, i, &ipid);
		if (ret)
			goto out;

		ret = ras_fw_get_timestamp(ras_core, i, &ts);
		if (ret)
			goto out;

		if (record_umc) {
			record_umc[i - rec_idx].address = mca;
			/* retired_page (pa) is unused now */
			record_umc[i - rec_idx].retired_row_pfn = 0x1ULL;
			record_umc[i - rec_idx].ts = ts;
			record_umc[i - rec_idx].err_type = RAS_EEPROM_ERR_NON_RECOVERABLE;

			ras_core->ras_umc.ip_func->mca_ipid_parse(ras_core, ipid,
				(uint32_t *)&(record_umc[i - rec_idx].cu),
				(uint32_t *)&(record_umc[i - rec_idx].mem_channel),
				(uint32_t *)&(record_umc[i - rec_idx].mcumc_id), NULL);

			/* update bad channel bitmap */
			if ((record_umc[i - rec_idx].mem_channel < BITS_PER_TYPE(control->bad_channel_bitmap)) &&
				!(control->bad_channel_bitmap & (1 << record_umc[i - rec_idx].mem_channel))) {
				control->bad_channel_bitmap |= 1 << record_umc[i - rec_idx].mem_channel;
				control->update_channel_flag = true;
			}
		}

		if (ras_ecc) {
			ras_ecc[i - rec_idx].addr = mca;
			ras_ecc[i - rec_idx].ipid = ipid;
			ras_ecc[i - rec_idx].ts = ts;
		}

	}

out:
	mutex_unlock(&control->ras_tbl_mutex);
	return ret;
}

uint32_t ras_fw_eeprom_get_record_count(struct ras_core_context *ras_core)
{
	if (!ras_core)
		return 0;

	return ras_core->ras_fw_eeprom.ras_num_recs;
}

int ras_fw_eeprom_update_record(struct ras_core_context *ras_core,
				struct ras_bank_ecc *ras_ecc)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	int ret, retry = 20;
	u32 recs_num_new = control->ras_num_recs;

	do {
		/* 1000ms timeout is long enough, smu_get_badpage_count won't
		 * return -EBUSY before timeout.
		 */
		ret = ras_fw_get_badpage_count(ras_core,
			&recs_num_new, RAS_SMU_MESSAGE_TIMEOUT_MS);
		if (!ret &&
		    (recs_num_new == control->ras_num_recs)) {
			/* record number update in PMFW needs some time,
			 * smu_get_badpage_count may return immediately without
			 * count update, sleep for a while and retry again.
			 */
			msleep(50);
			retry--;
		} else {
			break;
		}
	} while (retry);

	if (ret)
		return ret;

	if (recs_num_new > control->ras_num_recs)
		ret = ras_fw_eeprom_read_idx(ras_core, 0,
					ras_ecc, control->ras_num_recs, 1);
	else
		ret = -EINVAL;

	return ret;
}

static int __check_ras_fw_table_status(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	uint64_t local_time;
	int res;

	mutex_init(&control->ras_tbl_mutex);

	res = ras_fw_get_table_version(ras_core, &(control->version));
	if (res)
		return res;

	res = ras_fw_get_badpage_count(ras_core, &(control->ras_num_recs), 100);
	if (res)
		return res;

	local_time = (uint64_t)ktime_get_real_seconds();
	res = ras_fw_set_timestamp(ras_core, local_time);
	if (res)
		return res;

	control->ras_max_record_count = 4000;


	if (control->ras_num_recs > control->ras_max_record_count) {
		RAS_DEV_ERR(ras_core->dev,
			"RAS header invalid, records in header: %u max allowed :%u",
			control->ras_num_recs, control->ras_max_record_count);
		return -EINVAL;
	}

	return 0;
}

int ras_fw_eeprom_hw_init(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control;
	struct ras_eeprom_config *eeprom_cfg;
	struct ras_mp1 *mp1;
	const struct ras_mp1_sys_func *sys_func;

	if (!ras_core)
		return -EINVAL;

	mp1 = &ras_core->ras_mp1;
	sys_func = mp1->sys_func;

	if (!sys_func || !sys_func->mp1_send_eeprom_msg)
		return -EINVAL;

	ras_core->is_rma = false;

	control = &ras_core->ras_fw_eeprom;

	memset(control, 0, sizeof(*control));

	eeprom_cfg = &ras_core->config->eeprom_cfg;
	control->record_threshold_config =
		eeprom_cfg->eeprom_record_threshold_config;

	control->record_threshold_count = 4000;
	if (eeprom_cfg->eeprom_record_threshold_count <
		control->record_threshold_count)
		control->record_threshold_count =
			eeprom_cfg->eeprom_record_threshold_count;

	control->update_channel_flag = false;

	return __check_ras_fw_table_status(ras_core);
}

int ras_fw_eeprom_hw_fini(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control;

	if (!ras_core)
		return -EINVAL;

	control = &ras_core->ras_fw_eeprom;
	mutex_destroy(&control->ras_tbl_mutex);

	return 0;
}

int ras_fw_eeprom_check_storage_status(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;
	int bad_page_count;

	bad_page_count = ras_umc_get_badpage_count(ras_core);

	if ((control->record_threshold_count < bad_page_count) &&
	    (control->record_threshold_config != 0)) {
		RAS_DEV_ERR(ras_core->dev, "RAS records:%d exceed threshold:%d",
				bad_page_count, control->record_threshold_count);
		if ((control->record_threshold_config == WARN_NONSTOP_OVER_THRESHOLD) ||
			(control->record_threshold_config == NONSTOP_OVER_THRESHOLD)) {
			RAS_DEV_WARN(ras_core->dev,
			"Please consult AMD Service Action Guide (SAG) for appropriate service procedures\n");
		} else {
			ras_core->is_rma = true;
			RAS_DEV_ERR(ras_core->dev,
			"User defined threshold is set, runtime service will be halt when threshold is reached\n");
		}
		return 0;
	}

	RAS_DEV_INFO(ras_core->dev,
			"Found existing EEPROM table with %d records\n",
			bad_page_count);
	/* Warn if we are at 90% of the threshold or above
	 */
	if (10 * bad_page_count >= 9 * control->record_threshold_count)
		RAS_DEV_WARN(ras_core->dev,
			"RAS records:%u exceeds 90%% of threshold:%d\n",
			bad_page_count,
			control->record_threshold_count);

	return 0;
}

enum ras_gpu_health_status
	ras_fw_eeprom_check_gpu_status(struct ras_core_context *ras_core)
{
	struct ras_fw_eeprom_control *control = &ras_core->ras_fw_eeprom;

	if (!control->record_threshold_config)
		return RAS_GPU_HEALTH_NONE;

	if (ras_core->is_rma)
		return RAS_GPU_RETIRED__ECC_REACH_THRESHOLD;

	return RAS_GPU_HEALTH_USABLE;
}
