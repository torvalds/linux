// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86 FPU boot time init code:
 */
#include <asm/fpu/api.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/init.h>

#include "internal.h"
#include "legacy.h"
#include "xstate.h"

/*
 * Initialize the registers found in all CPUs, CR0 and CR4:
 */
static void fpu__init_cpu_generic(void)
{
	unsigned long cr0;
	unsigned long cr4_mask = 0;

	if (boot_cpu_has(X86_FEATURE_FXSR))
		cr4_mask |= X86_CR4_OSFXSR;
	if (boot_cpu_has(X86_FEATURE_XMM))
		cr4_mask |= X86_CR4_OSXMMEXCPT;
	if (cr4_mask)
		cr4_set_bits(cr4_mask);

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_TS|X86_CR0_EM); /* clear TS and EM */
	if (!boot_cpu_has(X86_FEATURE_FPU))
		cr0 |= X86_CR0_EM;
	write_cr0(cr0);

	/* Flush out any pending x87 state: */
#ifdef CONFIG_MATH_EMULATION
	if (!boot_cpu_has(X86_FEATURE_FPU))
		fpstate_init_soft(&current->thread.fpu.fpstate->regs.soft);
	else
#endif
		asm volatile ("fninit");
}

/*
 * Enable all supported FPU features. Called when a CPU is brought online:
 */
void fpu__init_cpu(void)
{
	fpu__init_cpu_generic();
	fpu__init_cpu_xstate();
}

static bool fpu__probe_without_cpuid(void)
{
	unsigned long cr0;
	u16 fsw, fcw;

	fsw = fcw = 0xffff;

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_TS | X86_CR0_EM);
	write_cr0(cr0);

	asm volatile("fninit ; fnstsw %0 ; fnstcw %1" : "+m" (fsw), "+m" (fcw));

	pr_info("x86/fpu: Probing for FPU: FSW=0x%04hx FCW=0x%04hx\n", fsw, fcw);

	return fsw == 0 && (fcw & 0x103f) == 0x003f;
}

static void fpu__init_system_early_generic(struct cpuinfo_x86 *c)
{
	if (!boot_cpu_has(X86_FEATURE_CPUID) &&
	    !test_bit(X86_FEATURE_FPU, (unsigned long *)cpu_caps_cleared)) {
		if (fpu__probe_without_cpuid())
			setup_force_cpu_cap(X86_FEATURE_FPU);
		else
			setup_clear_cpu_cap(X86_FEATURE_FPU);
	}

#ifndef CONFIG_MATH_EMULATION
	if (!test_cpu_cap(&boot_cpu_data, X86_FEATURE_FPU)) {
		pr_emerg("x86/fpu: Giving up, no FPU found and no math emulation present\n");
		for (;;)
			asm volatile("hlt");
	}
#endif
}

/*
 * Boot time FPU feature detection code:
 */
unsigned int mxcsr_feature_mask __ro_after_init = 0xffffffffu;
EXPORT_SYMBOL_GPL(mxcsr_feature_mask);

static void __init fpu__init_system_mxcsr(void)
{
	unsigned int mask = 0;

	if (boot_cpu_has(X86_FEATURE_FXSR)) {
		/* Static because GCC does not get 16-byte stack alignment right: */
		static struct fxregs_state fxregs __initdata;

		asm volatile("fxsave %0" : "+m" (fxregs));

		mask = fxregs.mxcsr_mask;

		/*
		 * If zero then use the default features mask,
		 * which has all features set, except the
		 * denormals-are-zero feature bit:
		 */
		if (mask == 0)
			mask = 0x0000ffbf;
	}
	mxcsr_feature_mask &= mask;
}

/*
 * Once per bootup FPU initialization sequences that will run on most x86 CPUs:
 */
static void __init fpu__init_system_generic(void)
{
	/*
	 * Set up the legacy init FPU context. Will be updated when the
	 * CPU supports XSAVE[S].
	 */
	fpstate_init_user(&init_fpstate);

	fpu__init_system_mxcsr();
}

/*
 * Enforce that 'MEMBER' is the last field of 'TYPE'.
 *
 * Align the computed size with alignment of the TYPE,
 * because that's how C aligns structs.
 */
#define CHECK_MEMBER_AT_END_OF(TYPE, MEMBER) \
	BUILD_BUG_ON(sizeof(TYPE) !=         \
		     ALIGN(offsetofend(TYPE, MEMBER), _Alignof(TYPE)))

/*
 * We append the 'struct fpu' to the task_struct:
 */
static void __init fpu__init_task_struct_size(void)
{
	int task_size = sizeof(struct task_struct);

	/*
	 * Subtract off the static size of the register state.
	 * It potentially has a bunch of padding.
	 */
	task_size -= sizeof(current->thread.fpu.__fpstate.regs);

	/*
	 * Add back the dynamically-calculated register state
	 * size.
	 */
	task_size += fpu_kernel_cfg.default_size;

	/*
	 * We dynamically size 'struct fpu', so we require that
	 * it be at the end of 'thread_struct' and that
	 * 'thread_struct' be at the end of 'task_struct'.  If
	 * you hit a compile error here, check the structure to
	 * see if something got added to the end.
	 */
	CHECK_MEMBER_AT_END_OF(struct fpu, __fpstate);
	CHECK_MEMBER_AT_END_OF(struct thread_struct, fpu);
	CHECK_MEMBER_AT_END_OF(struct task_struct, thread);

	arch_task_struct_size = task_size;
}

/*
 * Set up the user and kernel xstate sizes based on the legacy FPU context size.
 *
 * We set this up first, and later it will be overwritten by
 * fpu__init_system_xstate() if the CPU knows about xstates.
 */
static void __init fpu__init_system_xstate_size_legacy(void)
{
	unsigned int size;

	/*
	 * Note that the size configuration might be overwritten later
	 * during fpu__init_system_xstate().
	 */
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		size = sizeof(struct swregs_state);
	} else if (cpu_feature_enabled(X86_FEATURE_FXSR)) {
		size = sizeof(struct fxregs_state);
		fpu_user_cfg.legacy_features = XFEATURE_MASK_FPSSE;
	} else {
		size = sizeof(struct fregs_state);
		fpu_user_cfg.legacy_features = XFEATURE_MASK_FP;
	}

	fpu_kernel_cfg.max_size = size;
	fpu_kernel_cfg.default_size = size;
	fpu_user_cfg.max_size = size;
	fpu_user_cfg.default_size = size;
	fpstate_reset(&current->thread.fpu);
}

/*
 * Called on the boot CPU once per system bootup, to set up the initial
 * FPU state that is later cloned into all processes:
 */
void __init fpu__init_system(struct cpuinfo_x86 *c)
{
	fpstate_reset(&current->thread.fpu);
	fpu__init_system_early_generic(c);

	/*
	 * The FPU has to be operational for some of the
	 * later FPU init activities:
	 */
	fpu__init_cpu();

	fpu__init_system_generic();
	fpu__init_system_xstate_size_legacy();
	fpu__init_system_xstate(fpu_kernel_cfg.max_size);
	fpu__init_task_struct_size();
}
