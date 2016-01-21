/*
 * x86 FPU boot time init code:
 */
#include <asm/fpu/internal.h>
#include <asm/tlbflush.h>

#include <linux/sched.h>

/*
 * Initialize the TS bit in CR0 according to the style of context-switches
 * we are using:
 */
static void fpu__init_cpu_ctx_switch(void)
{
	if (!cpu_has_eager_fpu)
		stts();
	else
		clts();
}

/*
 * Initialize the registers found in all CPUs, CR0 and CR4:
 */
static void fpu__init_cpu_generic(void)
{
	unsigned long cr0;
	unsigned long cr4_mask = 0;

	if (cpu_has_fxsr)
		cr4_mask |= X86_CR4_OSFXSR;
	if (cpu_has_xmm)
		cr4_mask |= X86_CR4_OSXMMEXCPT;
	if (cr4_mask)
		cr4_set_bits(cr4_mask);

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_TS|X86_CR0_EM); /* clear TS and EM */
	if (!cpu_has_fpu)
		cr0 |= X86_CR0_EM;
	write_cr0(cr0);

	/* Flush out any pending x87 state: */
#ifdef CONFIG_MATH_EMULATION
	if (!cpu_has_fpu)
		fpstate_init_soft(&current->thread.fpu.state.soft);
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
	fpu__init_cpu_ctx_switch();
}

/*
 * The earliest FPU detection code.
 *
 * Set the X86_FEATURE_FPU CPU-capability bit based on
 * trying to execute an actual sequence of FPU instructions:
 */
static void fpu__init_system_early_generic(struct cpuinfo_x86 *c)
{
	unsigned long cr0;
	u16 fsw, fcw;

	fsw = fcw = 0xffff;

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_TS | X86_CR0_EM);
	write_cr0(cr0);

	asm volatile("fninit ; fnstsw %0 ; fnstcw %1"
		     : "+m" (fsw), "+m" (fcw));

	if (fsw == 0 && (fcw & 0x103f) == 0x003f)
		set_cpu_cap(c, X86_FEATURE_FPU);
	else
		clear_cpu_cap(c, X86_FEATURE_FPU);

#ifndef CONFIG_MATH_EMULATION
	if (!cpu_has_fpu) {
		pr_emerg("x86/fpu: Giving up, no FPU found and no math emulation present\n");
		for (;;)
			asm volatile("hlt");
	}
#endif
}

/*
 * Boot time FPU feature detection code:
 */
unsigned int mxcsr_feature_mask __read_mostly = 0xffffffffu;

static void __init fpu__init_system_mxcsr(void)
{
	unsigned int mask = 0;

	if (cpu_has_fxsr) {
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
	 * Set up the legacy init FPU context. (xstate init might overwrite this
	 * with a more modern format, if the CPU supports it.)
	 */
	fpstate_init_fxstate(&init_fpstate.fxsave);

	fpu__init_system_mxcsr();
}

/*
 * Size of the FPU context state. All tasks in the system use the
 * same context size, regardless of what portion they use.
 * This is inherent to the XSAVE architecture which puts all state
 * components into a single, continuous memory block:
 */
unsigned int xstate_size;
EXPORT_SYMBOL_GPL(xstate_size);

/* Enforce that 'MEMBER' is the last field of 'TYPE': */
#define CHECK_MEMBER_AT_END_OF(TYPE, MEMBER) \
	BUILD_BUG_ON(sizeof(TYPE) != offsetofend(TYPE, MEMBER))

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
	task_size -= sizeof(((struct task_struct *)0)->thread.fpu.state);

	/*
	 * Add back the dynamically-calculated register state
	 * size.
	 */
	task_size += xstate_size;

	/*
	 * We dynamically size 'struct fpu', so we require that
	 * it be at the end of 'thread_struct' and that
	 * 'thread_struct' be at the end of 'task_struct'.  If
	 * you hit a compile error here, check the structure to
	 * see if something got added to the end.
	 */
	CHECK_MEMBER_AT_END_OF(struct fpu, state);
	CHECK_MEMBER_AT_END_OF(struct thread_struct, fpu);
	CHECK_MEMBER_AT_END_OF(struct task_struct, thread);

	arch_task_struct_size = task_size;
}

/*
 * Set up the xstate_size based on the legacy FPU context size.
 *
 * We set this up first, and later it will be overwritten by
 * fpu__init_system_xstate() if the CPU knows about xstates.
 */
static void __init fpu__init_system_xstate_size_legacy(void)
{
	static int on_boot_cpu = 1;

	WARN_ON_FPU(!on_boot_cpu);
	on_boot_cpu = 0;

	/*
	 * Note that xstate_size might be overwriten later during
	 * fpu__init_system_xstate().
	 */

	if (!cpu_has_fpu) {
		/*
		 * Disable xsave as we do not support it if i387
		 * emulation is enabled.
		 */
		setup_clear_cpu_cap(X86_FEATURE_XSAVE);
		setup_clear_cpu_cap(X86_FEATURE_XSAVEOPT);
		xstate_size = sizeof(struct swregs_state);
	} else {
		if (cpu_has_fxsr)
			xstate_size = sizeof(struct fxregs_state);
		else
			xstate_size = sizeof(struct fregs_state);
	}
	/*
	 * Quirk: we don't yet handle the XSAVES* instructions
	 * correctly, as we don't correctly convert between
	 * standard and compacted format when interfacing
	 * with user-space - so disable it for now.
	 *
	 * The difference is small: with recent CPUs the
	 * compacted format is only marginally smaller than
	 * the standard FPU state format.
	 *
	 * ( This is easy to backport while we are fixing
	 *   XSAVES* support. )
	 */
	setup_clear_cpu_cap(X86_FEATURE_XSAVES);
}

/*
 * FPU context switching strategies:
 *
 * Against popular belief, we don't do lazy FPU saves, due to the
 * task migration complications it brings on SMP - we only do
 * lazy FPU restores.
 *
 * 'lazy' is the traditional strategy, which is based on setting
 * CR0::TS to 1 during context-switch (instead of doing a full
 * restore of the FPU state), which causes the first FPU instruction
 * after the context switch (whenever it is executed) to fault - at
 * which point we lazily restore the FPU state into FPU registers.
 *
 * Tasks are of course under no obligation to execute FPU instructions,
 * so it can easily happen that another context-switch occurs without
 * a single FPU instruction being executed. If we eventually switch
 * back to the original task (that still owns the FPU) then we have
 * not only saved the restores along the way, but we also have the
 * FPU ready to be used for the original task.
 *
 * 'eager' switching is used on modern CPUs, there we switch the FPU
 * state during every context switch, regardless of whether the task
 * has used FPU instructions in that time slice or not. This is done
 * because modern FPU context saving instructions are able to optimize
 * state saving and restoration in hardware: they can detect both
 * unused and untouched FPU state and optimize accordingly.
 *
 * [ Note that even in 'lazy' mode we might optimize context switches
 *   to use 'eager' restores, if we detect that a task is using the FPU
 *   frequently. See the fpu->counter logic in fpu/internal.h for that. ]
 */
static enum { AUTO, ENABLE, DISABLE } eagerfpu = AUTO;

static int __init eager_fpu_setup(char *s)
{
	if (!strcmp(s, "on"))
		eagerfpu = ENABLE;
	else if (!strcmp(s, "off"))
		eagerfpu = DISABLE;
	else if (!strcmp(s, "auto"))
		eagerfpu = AUTO;
	return 1;
}
__setup("eagerfpu=", eager_fpu_setup);

/*
 * Pick the FPU context switching strategy:
 */
static void __init fpu__init_system_ctx_switch(void)
{
	static bool on_boot_cpu = 1;

	WARN_ON_FPU(!on_boot_cpu);
	on_boot_cpu = 0;

	WARN_ON_FPU(current->thread.fpu.fpstate_active);
	current_thread_info()->status = 0;

	/* Auto enable eagerfpu for xsaveopt */
	if (cpu_has_xsaveopt && eagerfpu != DISABLE)
		eagerfpu = ENABLE;

	if (xfeatures_mask & XFEATURE_MASK_EAGER) {
		if (eagerfpu == DISABLE) {
			pr_err("x86/fpu: eagerfpu switching disabled, disabling the following xstate features: 0x%llx.\n",
			       xfeatures_mask & XFEATURE_MASK_EAGER);
			xfeatures_mask &= ~XFEATURE_MASK_EAGER;
		} else {
			eagerfpu = ENABLE;
		}
	}

	if (eagerfpu == ENABLE)
		setup_force_cpu_cap(X86_FEATURE_EAGER_FPU);

	printk(KERN_INFO "x86/fpu: Using '%s' FPU context switches.\n", eagerfpu == ENABLE ? "eager" : "lazy");
}

/*
 * Called on the boot CPU once per system bootup, to set up the initial
 * FPU state that is later cloned into all processes:
 */
void __init fpu__init_system(struct cpuinfo_x86 *c)
{
	fpu__init_system_early_generic(c);

	/*
	 * The FPU has to be operational for some of the
	 * later FPU init activities:
	 */
	fpu__init_cpu();

	/*
	 * But don't leave CR0::TS set yet, as some of the FPU setup
	 * methods depend on being able to execute FPU instructions
	 * that will fault on a set TS, such as the FXSAVE in
	 * fpu__init_system_mxcsr().
	 */
	clts();

	fpu__init_system_generic();
	fpu__init_system_xstate_size_legacy();
	fpu__init_system_xstate();
	fpu__init_task_struct_size();

	fpu__init_system_ctx_switch();
}

/*
 * Boot parameter to turn off FPU support and fall back to math-emu:
 */
static int __init no_387(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_FPU);
	return 1;
}
__setup("no387", no_387);

/*
 * Disable all xstate CPU features:
 */
static int __init x86_noxsave_setup(char *s)
{
	if (strlen(s))
		return 0;

	fpu__xstate_clear_all_cpu_caps();

	return 1;
}
__setup("noxsave", x86_noxsave_setup);

/*
 * Disable the XSAVEOPT instruction specifically:
 */
static int __init x86_noxsaveopt_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_XSAVEOPT);

	return 1;
}
__setup("noxsaveopt", x86_noxsaveopt_setup);

/*
 * Disable the XSAVES instruction:
 */
static int __init x86_noxsaves_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_XSAVES);

	return 1;
}
__setup("noxsaves", x86_noxsaves_setup);

/*
 * Disable FX save/restore and SSE support:
 */
static int __init x86_nofxsr_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_FXSR);
	setup_clear_cpu_cap(X86_FEATURE_FXSR_OPT);
	setup_clear_cpu_cap(X86_FEATURE_XMM);

	return 1;
}
__setup("nofxsr", x86_nofxsr_setup);
