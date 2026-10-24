// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "ras_mp1_v13_0.h"
#include "ras_mp1_v15_0.h"

static const struct ras_mp1_ip_func *ras_mp1_get_ip_funcs(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
	case IP_VERSION(13, 0, 15):
		return &mp1_ras_func_v13_0;
	case IP_VERSION(15, 0, 8):
	case IP_VERSION(15, 0, 3):
		return &mp1_ras_func_v15_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"MP1 ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_mp1_get_bank_count(struct ras_core_context *ras_core,
			    enum ras_err_type type, u32 *count)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!mp1->ip_func || !mp1->ip_func->get_valid_bank_count)
		return 0;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->get_valid_bank_count(ras_core, type, count);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_dump_bank(struct ras_core_context *ras_core,
		u32 type, u32 idx, u64 *regs, u32 regs_sz)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!mp1->ip_func || !mp1->ip_func->dump_valid_bank)
		return 0;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->dump_valid_bank(ras_core,
				type, idx, regs, regs_sz);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_get_table_version(struct ras_core_context *ras_core,
		u32 *table_version)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!mp1->ip_func || !mp1->ip_func->get_table_version)
		return -EOPNOTSUPP;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->get_table_version(ras_core, table_version);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

bool ras_mp1_rma_detected(struct ras_core_context *ras_core)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!mp1->ip_func || !mp1->ip_func->rma_detected)
		return false;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->rma_detected(ras_core);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_set_timestamp(struct ras_core_context *ras_core,
		u64 timestamp)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!mp1->ip_func || !mp1->ip_func->set_timestamp)
		return -EOPNOTSUPP;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->set_timestamp(ras_core, timestamp);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_reset_ras_table(struct ras_core_context *ras_core,
		u32 *result)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!result || !mp1->ip_func || !mp1->ip_func->reset_ras_table)
		return -EOPNOTSUPP;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->reset_ras_table(ras_core, result);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_get_record_count(struct ras_core_context *ras_core, u32 *count)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!count || !mp1->ip_func || !mp1->ip_func->get_record_count)
		return -EOPNOTSUPP;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->get_record_count(ras_core, count);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_get_record(struct ras_core_context *ras_core,
		u32 idx, struct eeprom_err_record *rec)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret;

	if (!rec || !mp1->ip_func || !mp1->ip_func->get_record)
		return -EOPNOTSUPP;

	mutex_lock(&mp1->op_mutex);
	ret = mp1->ip_func->get_record(ras_core, idx, rec);
	mutex_unlock(&mp1->op_mutex);

	return ret;
}

int ras_mp1_set_debug_mode(struct ras_core_context *ras_core, bool enable)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;

	if (!mp1->ip_func || !mp1->ip_func->set_debug_mode)
		return -EOPNOTSUPP;

	return mp1->ip_func->set_debug_mode(ras_core, enable);
}

int ras_mp1_check_feature_status(struct ras_core_context *ras_core,
		enum ras_mp1_feature_id feature_id, u32 *status)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;

	if (!mp1->sys_func ||
	    !mp1->sys_func->check_ras_feature_status)
		return -EOPNOTSUPP;

	return mp1->sys_func->check_ras_feature_status(ras_core,
				feature_id, status);
}

int ras_mp1_sw_init(struct ras_core_context *ras_core)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	int ret = 0;

	if (!ras_core->config)
		return -EINVAL;

	mp1->mp1_ip_version = ras_core->config->mp1_ip_version;

	if (ras_core->config && ras_core->config->mp1_cfg.mp1_sys_fn)
		mp1->sys_func = ras_core->config->mp1_cfg.mp1_sys_fn;

	mp1->ip_func = ras_mp1_get_ip_funcs(ras_core, mp1->mp1_ip_version);
	if (!mp1->ip_func)
		return -EINVAL;

	mutex_init(&mp1->op_mutex);

	/* Nothing else in the MP1 block depends on the debug mode control. */
	ret = ras_mp1_set_debug_mode(ras_core, false);
	if (ret && ret != -EOPNOTSUPP) {
		mutex_destroy(&mp1->op_mutex);
		return ret;
	}

	return 0;
}

int ras_mp1_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;

	mutex_destroy(&mp1->op_mutex);

	return 0;
}
