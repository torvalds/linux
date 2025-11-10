/* SPDX-License-Identifier: MIT */
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
#ifndef __RAS_MP1_H__
#define __RAS_MP1_H__
#include "ras.h"

enum ras_err_type;
struct ras_mp1_ip_func {
	int (*get_valid_bank_count)(struct ras_core_context *ras_core,
			enum ras_err_type type, u32 *count);
	int (*dump_valid_bank)(struct ras_core_context *ras_core,
		enum ras_err_type type, u32 idx, u32 reg_idx, u64 *val);
};

struct ras_mp1 {
	uint32_t mp1_ip_version;
	const struct ras_mp1_ip_func *ip_func;
	const struct ras_mp1_sys_func *sys_func;
};

int ras_mp1_hw_init(struct ras_core_context *ras_core);
int ras_mp1_hw_fini(struct ras_core_context *ras_core);

int ras_mp1_get_bank_count(struct ras_core_context *ras_core,
			    enum ras_err_type type, u32 *count);

int ras_mp1_dump_bank(struct ras_core_context *ras_core,
		u32 ecc_type, u32 idx, u32 reg_idx, u64 *val);
#endif
