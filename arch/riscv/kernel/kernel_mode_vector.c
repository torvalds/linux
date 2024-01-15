// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2021 SiFive
 */
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/types.h>

#include <asm/vector.h>
#include <asm/switch_to.h>
#include <asm/simd.h>

static inline void riscv_v_flags_set(u32 flags)
{
	current->thread.riscv_v_flags = flags;
}

static inline void riscv_v_start(u32 flags)
{
	int orig;

	orig = riscv_v_flags();
	BUG_ON((orig & flags) != 0);
	riscv_v_flags_set(orig | flags);
}

static inline void riscv_v_stop(u32 flags)
{
	int orig;

	orig = riscv_v_flags();
	BUG_ON((orig & flags) == 0);
	riscv_v_flags_set(orig & ~flags);
}

/*
 * Claim ownership of the CPU vector context for use by the calling context.
 *
 * The caller may freely manipulate the vector context metadata until
 * put_cpu_vector_context() is called.
 */
void get_cpu_vector_context(void)
{
	/*
	 * disable softirqs so it is impossible for softirqs to nest
	 * get_cpu_vector_context() when kernel is actively using Vector.
	 */
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_disable();
	else
		preempt_disable();

	riscv_v_start(RISCV_KERNEL_MODE_V);
}

/*
 * Release the CPU vector context.
 *
 * Must be called from a context in which get_cpu_vector_context() was
 * previously called, with no call to put_cpu_vector_context() in the
 * meantime.
 */
void put_cpu_vector_context(void)
{
	riscv_v_stop(RISCV_KERNEL_MODE_V);

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_enable();
	else
		preempt_enable();
}

/*
 * kernel_vector_begin(): obtain the CPU vector registers for use by the calling
 * context
 *
 * Must not be called unless may_use_simd() returns true.
 * Task context in the vector registers is saved back to memory as necessary.
 *
 * A matching call to kernel_vector_end() must be made before returning from the
 * calling context.
 *
 * The caller may freely use the vector registers until kernel_vector_end() is
 * called.
 */
void kernel_vector_begin(void)
{
	if (WARN_ON(!has_vector()))
		return;

	BUG_ON(!may_use_simd());

	get_cpu_vector_context();

	riscv_v_vstate_save(current, task_pt_regs(current));

	riscv_v_enable();
}
EXPORT_SYMBOL_GPL(kernel_vector_begin);

/*
 * kernel_vector_end(): give the CPU vector registers back to the current task
 *
 * Must be called from a context in which kernel_vector_begin() was previously
 * called, with no call to kernel_vector_end() in the meantime.
 *
 * The caller must not use the vector registers after this function is called,
 * unless kernel_vector_begin() is called again in the meantime.
 */
void kernel_vector_end(void)
{
	if (WARN_ON(!has_vector()))
		return;

	riscv_v_vstate_set_restore(current, task_pt_regs(current));

	riscv_v_disable();

	put_cpu_vector_context();
}
EXPORT_SYMBOL_GPL(kernel_vector_end);
