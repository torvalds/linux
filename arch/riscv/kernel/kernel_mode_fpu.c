// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 SiFive
 */

#include <linux/export.h>
#include <linux/preempt.h>

#include <asm/csr.h>
#include <asm/fpu.h>
#include <asm/processor.h>
#include <asm/switch_to.h>

void kernel_fpu_begin(void)
{
	preempt_disable();
	fstate_save(current, task_pt_regs(current));
	csr_set(CSR_SSTATUS, SR_FS);
}
EXPORT_SYMBOL_GPL(kernel_fpu_begin);

void kernel_fpu_end(void)
{
	csr_clear(CSR_SSTATUS, SR_FS);
	fstate_restore(current, task_pt_regs(current));
	preempt_enable();
}
EXPORT_SYMBOL_GPL(kernel_fpu_end);
