// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <asm/fpu.h>
#include <asm/smp.h>

static DEFINE_PER_CPU(bool, in_kernel_fpu);

void kernel_fpu_begin(void)
{
	preempt_disable();

	WARN_ON(this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, true);

	if (!is_fpu_owner())
		enable_fpu();
	else
		_save_fp(&current->thread.fpu);

	write_fcsr(LOONGARCH_FCSR0, 0);
}
EXPORT_SYMBOL_GPL(kernel_fpu_begin);

void kernel_fpu_end(void)
{
	WARN_ON(!this_cpu_read(in_kernel_fpu));

	if (!is_fpu_owner())
		disable_fpu();
	else
		_restore_fp(&current->thread.fpu);

	this_cpu_write(in_kernel_fpu, false);

	preempt_enable();
}
EXPORT_SYMBOL_GPL(kernel_fpu_end);
