// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <asm/fpu.h>
#include <asm/smp.h>

static unsigned int euen_mask = CSR_EUEN_FPEN;

/*
 * The critical section between kernel_fpu_begin() and kernel_fpu_end()
 * is non-reentrant. It is the caller's responsibility to avoid reentrance.
 * See drivers/gpu/drm/amd/display/amdgpu_dm/dc_fpu.c as an example.
 */
static DEFINE_PER_CPU(bool, in_kernel_fpu);
static DEFINE_PER_CPU(unsigned int, euen_current);

static inline void fpregs_lock(void)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();
	else
		local_bh_disable();
}

static inline void fpregs_unlock(void)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
	else
		local_bh_enable();
}

void kernel_fpu_begin(void)
{
	unsigned int *euen_curr;

	if (!irqs_disabled())
		fpregs_lock();

	WARN_ON(this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, true);
	euen_curr = this_cpu_ptr(&euen_current);

	*euen_curr = csr_xchg32(euen_mask, euen_mask, LOONGARCH_CSR_EUEN);

#ifdef CONFIG_CPU_HAS_LASX
	if (*euen_curr & CSR_EUEN_LASXEN)
		_save_lasx(&current->thread.fpu);
	else
#endif
#ifdef CONFIG_CPU_HAS_LSX
	if (*euen_curr & CSR_EUEN_LSXEN)
		_save_lsx(&current->thread.fpu);
	else
#endif
	if (*euen_curr & CSR_EUEN_FPEN)
		_save_fp(&current->thread.fpu);

	write_fcsr(LOONGARCH_FCSR0, 0);
}
EXPORT_SYMBOL_GPL(kernel_fpu_begin);

void kernel_fpu_end(void)
{
	unsigned int *euen_curr;

	WARN_ON(!this_cpu_read(in_kernel_fpu));

	euen_curr = this_cpu_ptr(&euen_current);

#ifdef CONFIG_CPU_HAS_LASX
	if (*euen_curr & CSR_EUEN_LASXEN)
		_restore_lasx(&current->thread.fpu);
	else
#endif
#ifdef CONFIG_CPU_HAS_LSX
	if (*euen_curr & CSR_EUEN_LSXEN)
		_restore_lsx(&current->thread.fpu);
	else
#endif
	if (*euen_curr & CSR_EUEN_FPEN)
		_restore_fp(&current->thread.fpu);

	*euen_curr = csr_xchg32(*euen_curr, euen_mask, LOONGARCH_CSR_EUEN);

	this_cpu_write(in_kernel_fpu, false);

	if (!irqs_disabled())
		fpregs_unlock();
}
EXPORT_SYMBOL_GPL(kernel_fpu_end);

static int __init init_euen_mask(void)
{
	if (cpu_has_lsx)
		euen_mask |= CSR_EUEN_LSXEN;

	if (cpu_has_lasx)
		euen_mask |= CSR_EUEN_LASXEN;

	return 0;
}
arch_initcall(init_euen_mask);
