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

static const struct ras_mp1_ip_func *ras_mp1_get_ip_funcs(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
		return &mp1_ras_func_v13_0;
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

	return mp1->ip_func->get_valid_bank_count(ras_core, type, count);
}

int ras_mp1_dump_bank(struct ras_core_context *ras_core,
		u32 type, u32 idx, u32 reg_idx, u64 *val)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;

	return mp1->ip_func->dump_valid_bank(ras_core, type, idx, reg_idx, val);
}

int ras_mp1_hw_init(struct ras_core_context *ras_core)
{
	struct ras_mp1 *mp1 = &ras_core->ras_mp1;

	mp1->mp1_ip_version = ras_core->config->mp1_ip_version;
	mp1->sys_func = ras_core->config->mp1_cfg.mp1_sys_fn;
	if (!mp1->sys_func) {
		RAS_DEV_ERR(ras_core->dev, "RAS mp1 sys function not configured!\n");
		return -EINVAL;
	}

	mp1->ip_func = ras_mp1_get_ip_funcs(ras_core, mp1->mp1_ip_version);

	return mp1->ip_func ? RAS_CORE_OK : -EINVAL;
}

int ras_mp1_hw_fini(struct ras_core_context *ras_core)
{
	return 0;
}
