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
#include "ras_core_status.h"
#include "ras_mp1_v13_0.h"

#define RAS_MP1_MSG_QueryValidMcaCount                0x36
#define RAS_MP1_MSG_McaBankDumpDW                     0x37
#define RAS_MP1_MSG_ClearMcaOnRead                    0x39
#define RAS_MP1_MSG_QueryValidMcaCeCount              0x3A
#define RAS_MP1_MSG_McaBankCeDumpDW                   0x3B

#define MAX_UE_BANKS_PER_QUERY  12
#define MAX_CE_BANKS_PER_QUERY  12

static int mp1_v13_0_get_bank_count(struct ras_core_context *ras_core,
			    enum ras_err_type type, u32 *count)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	uint32_t bank_count = 0;
	u32 msg;
	int ret;

	if (!count)
		return -EINVAL;

	if (!sys_func || !sys_func->mp1_get_valid_bank_count)
		return -RAS_CORE_NOT_SUPPORTED;

	switch (type) {
	case RAS_ERR_TYPE__UE:
		msg = RAS_MP1_MSG_QueryValidMcaCount;
		break;
	case RAS_ERR_TYPE__CE:
	case RAS_ERR_TYPE__DE:
		msg = RAS_MP1_MSG_QueryValidMcaCeCount;
		break;
	default:
		return -EINVAL;
	}

	ret = sys_func->mp1_get_valid_bank_count(ras_core, msg, &bank_count);
	if (!ret) {
		if (((type == RAS_ERR_TYPE__UE) && (bank_count >= MAX_UE_BANKS_PER_QUERY)) ||
			((type == RAS_ERR_TYPE__CE) && (bank_count >= MAX_CE_BANKS_PER_QUERY)))
			return -EINVAL;

		*count = bank_count;
	}

	return ret;
}

static int mp1_v13_0_dump_bank(struct ras_core_context *ras_core,
			enum ras_err_type type, u32 idx, u32 reg_idx, u64 *val)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;
	const struct ras_mp1_sys_func *sys_func = mp1->sys_func;
	u32 msg;

	if (!sys_func || !sys_func->mp1_dump_valid_bank)
		return -RAS_CORE_NOT_SUPPORTED;

	switch (type) {
	case RAS_ERR_TYPE__UE:
		msg = RAS_MP1_MSG_McaBankDumpDW;
		break;
	case RAS_ERR_TYPE__CE:
	case RAS_ERR_TYPE__DE:
		msg = RAS_MP1_MSG_McaBankCeDumpDW;
		break;
	default:
		return -EINVAL;
	}

	return sys_func->mp1_dump_valid_bank(ras_core, msg, idx, reg_idx, val);
}

const struct ras_mp1_ip_func mp1_ras_func_v13_0 = {
	.get_valid_bank_count = mp1_v13_0_get_bank_count,
	.dump_valid_bank = mp1_v13_0_dump_bank,
};
