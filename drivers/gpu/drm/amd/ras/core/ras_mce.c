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
#include "ras_mce.h"
#define RAS_MCE_BANK_FIFO_SIZE  (1024 * sizeof(struct aca_bank_reg))
#define RAS_MCE_BANK_COUNT(x) ((x) / sizeof(struct aca_bank_reg))

struct mce_smca_bank_range {
	u32 start;
	u32 end;
};

static const struct mce_smca_bank_range mce_smca_bank_range_v5[] = {
	[MCE_BANK_TYPE_GPU] = {32, 63},
	[MCE_BANK_TYPE_CPU] = {0, 31},
};

bool ras_mce_check_bank(struct ras_core_context *ras_core,
		enum mce_bank_type type, u32 bank)
{
	const struct mce_smca_bank_range *range;
	u32 aca_ip_version = 0;

	if (!ras_core || type >= MCE_BANK_TYPE_MAX)
		return false;

	if (ras_core_get_ip_version(ras_core,
				RAS_UNIT_ID_ACA, &aca_ip_version))
		return false;

	switch (aca_ip_version) {
	case IP_VERSION(5, 0, 0):
		range = &mce_smca_bank_range_v5[type];
		break;
	default:
		return false;
	}

	return  ((bank >= range->start) && (bank <= range->end)) ? true : false;
}

static int ras_mce_add_gpu_bank(struct ras_core_context *ras_core,
			struct aca_bank_reg *aca_bank)
{
	struct ras_mce *mce = &ras_core->ras_mce;
	int ret;

	ret = kfifo_in_spinlocked(&mce->mce_fifo,
			aca_bank, sizeof(*aca_bank), &mce->mce_fifo_lock);
	if (ret)
		ras_process_add_interrupt_req(ras_core, NULL, false);

	return ret ? 0 : -ENOSPC;
}

static int ras_mce_log_cpu_bank(struct ras_core_context *ras_core,
			struct aca_bank_reg *aca_bank)
{
	struct ras_cpu_mce cpu_mce = {
		.apic_id = aca_bank->apic_id,
		.bank = aca_bank->bank,
	};

	memcpy(cpu_mce.regs, aca_bank->regs, sizeof(cpu_mce.regs));
	ras_log_ring_add_log_event(ras_core,
		RAS_LOG_EVENT_CPU_RAS, &cpu_mce, sizeof(cpu_mce), NULL);

	return 0;
}

int ras_mce_sw_init(struct ras_core_context *ras_core)
{
	struct ras_mce *mce = &ras_core->ras_mce;

	if (kfifo_alloc(&mce->mce_fifo, RAS_MCE_BANK_FIFO_SIZE, GFP_KERNEL))
		return -ENOMEM;

	spin_lock_init(&mce->mce_fifo_lock);

	return 0;
}

int ras_mce_sw_fini(struct ras_core_context *ras_core)
{
	kfifo_free(&ras_core->ras_mce.mce_fifo);

	return 0;
}

int ras_mce_get_bank_count(struct ras_core_context *ras_core,
				enum ras_err_type type, u32 *count)
{
	u32 data_count;

	if (!ras_core || !count)
		return -EINVAL;

	data_count = RAS_MCE_BANK_COUNT(kfifo_len(&ras_core->ras_mce.mce_fifo));

	*count = (data_count <= MAX_RECORD_PER_BATCH) ? data_count : MAX_RECORD_PER_BATCH;

	return 0;
}

int ras_mce_dump_bank(struct ras_core_context *ras_core,
			u32 type, u32 idx, struct aca_bank_reg *reg_bank)
{
	struct ras_mce *mce = &ras_core->ras_mce;
	int ret;

	if (!ras_core || !reg_bank)
		return -EINVAL;

	ret = kfifo_out_spinlocked(&mce->mce_fifo,
				reg_bank, sizeof(*reg_bank), &mce->mce_fifo_lock);

	return ret ? 0 : -ENOENT;
}

int ras_mce_add_aca_bank(struct ras_core_context *ras_core,
		struct aca_bank_reg *aca_bank)
{
	if (!ras_core || !aca_bank)
		return -EINVAL;

	return (aca_bank->bank_type == MCE_BANK_TYPE_GPU) ?
		ras_mce_add_gpu_bank(ras_core, aca_bank) :
		ras_mce_log_cpu_bank(ras_core, aca_bank);
}
