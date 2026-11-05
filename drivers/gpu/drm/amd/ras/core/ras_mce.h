/* SPDX-License-Identifier: MIT */
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
#ifndef __RAS_MCE_H__
#define __RAS_MCE_H__

enum mce_bank_type {
	MCE_BANK_TYPE_GPU,
	MCE_BANK_TYPE_CPU,
	MCE_BANK_TYPE_MAX
};

struct ras_mce {
	struct kfifo mce_fifo;
	spinlock_t mce_fifo_lock;
};

int ras_mce_sw_init(struct ras_core_context *ras_core);
int ras_mce_sw_fini(struct ras_core_context *ras_core);

int ras_mce_get_bank_count(struct ras_core_context *ras_core,
	enum ras_err_type type, u32 *count);
int ras_mce_dump_bank(struct ras_core_context *ras_core,
	u32 type, u32 idx, struct aca_bank_reg *aca_bank);

bool ras_mce_check_bank(struct ras_core_context *ras_core,
		enum mce_bank_type, u32 bank);
int ras_mce_add_aca_bank(struct ras_core_context *ras_core,
		struct aca_bank_reg *aca_bank);

#endif
