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
