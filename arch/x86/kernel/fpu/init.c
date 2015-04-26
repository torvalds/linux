/*
 * x86 FPU boot time init code
 */
#include <asm/fpu/internal.h>
#include <asm/tlbflush.h>

/*
 * Boot time CPU/FPU FDIV bug detection code:
 */

static double __initdata x = 4195835.0;
static double __initdata y = 3145727.0;

/*
 * This used to check for exceptions..
 * However, it turns out that to support that,
 * the XMM trap handlers basically had to
 * be buggy. So let's have a correct XMM trap
 * handler, and forget about printing out
 * some status at boot.
 *
 * We should really only care about bugs here
 * anyway. Not features.
 */
static void __init check_fpu(void)
{
	s32 fdiv_bug;

	kernel_fpu_begin();

	/*
	 * trap_init() enabled FXSR and company _before_ testing for FP
	 * problems here.
	 *
	 * Test for the divl bug: http://en.wikipedia.org/wiki/Fdiv_bug
	 */
	__asm__("fninit\n\t"
		"fldl %1\n\t"
		"fdivl %2\n\t"
		"fmull %2\n\t"
		"fldl %1\n\t"
		"fsubp %%st,%%st(1)\n\t"
		"fistpl %0\n\t"
		"fwait\n\t"
		"fninit"
		: "=m" (*&fdiv_bug)
		: "m" (*&x), "m" (*&y));

	kernel_fpu_end();

	if (fdiv_bug) {
		set_cpu_bug(&boot_cpu_data, X86_BUG_FDIV);
		pr_warn("Hmm, FPU with FDIV bug\n");
	}
}

void fpu__init_check_bugs(void)
{
	/*
	 * kernel_fpu_begin/end() in check_fpu() relies on the patched
	 * alternative instructions.
	 */
	if (cpu_has_fpu)
		check_fpu();
}

/*
 * Boot time FPU feature detection code:
 */
unsigned int mxcsr_feature_mask __read_mostly = 0xffffffffu;

unsigned int xstate_size;
EXPORT_SYMBOL_GPL(xstate_size);

static void mxcsr_feature_mask_init(void)
{
	unsigned int mask = 0;

	if (cpu_has_fxsr) {
		struct i387_fxsave_struct fx_tmp __aligned(32) = { };

		asm volatile("fxsave %0" : "+m" (fx_tmp));

		mask = fx_tmp.mxcsr_mask;

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

static void fpstate_xstate_init_size(void)
{
	static bool on_boot_cpu = 1;

	if (!on_boot_cpu)
		return;
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
		xstate_size = sizeof(struct i387_soft_struct);
	} else {
		if (cpu_has_fxsr)
			xstate_size = sizeof(struct i387_fxsave_struct);
		else
			xstate_size = sizeof(struct i387_fsave_struct);
	}
}

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
 * Enable all supported FPU features. Called when a CPU is brought online.
 */
void fpu__init_cpu(void)
{
	unsigned long cr0;
	unsigned long cr4_mask = 0;

#ifndef CONFIG_MATH_EMULATION
	if (!cpu_has_fpu) {
		pr_emerg("No FPU found and no math emulation present\n");
		pr_emerg("Giving up\n");
		for (;;)
			asm volatile("hlt");
	}
#endif
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

	fpu__init_cpu_xstate();
	fpu__init_cpu_ctx_switch();
}

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
 * setup_init_fpu_buf() is __init and it is OK to call it here because
 * init_xstate_ctx will be unset only once during boot.
 */
static void fpu__init_system_ctx_switch(void)
{
	WARN_ON(current->thread.fpu.fpstate_active);
	current_thread_info()->status = 0;

	/* Auto enable eagerfpu for xsaveopt */
	if (cpu_has_xsaveopt && eagerfpu != DISABLE)
		eagerfpu = ENABLE;

	if (xfeatures_mask & XSTATE_EAGER) {
		if (eagerfpu == DISABLE) {
			pr_err("x86/fpu: eagerfpu switching disabled, disabling the following xstate features: 0x%llx.\n",
			       xfeatures_mask & XSTATE_EAGER);
			xfeatures_mask &= ~XSTATE_EAGER;
		} else {
			eagerfpu = ENABLE;
		}
	}

	if (eagerfpu == ENABLE)
		setup_force_cpu_cap(X86_FEATURE_EAGER_FPU);

	printk_once(KERN_INFO "x86/fpu: Using '%s' FPU context switches.\n", eagerfpu == ENABLE ? "eager" : "lazy");
}

/*
 * Called on the boot CPU once per system bootup, to set up the initial FPU state that
 * is later cloned into all processes.
 */
void fpu__init_system(void)
{
	/* The FPU has to be operational for some of the later FPU init activities: */
	fpu__init_cpu();

	/*
	 * But don't leave CR0::TS set yet, as some of the FPU setup methods depend
	 * on being able to execute FPU instructions that will fault on a set TS,
	 * such as the FXSAVE in mxcsr_feature_mask_init().
	 */
	clts();

	/*
	 * Set up the legacy init FPU context. (xstate init might overwrite this
	 * with a more modern format, if the CPU supports it.)
	 */
	fx_finit(&init_xstate_ctx.i387);

	mxcsr_feature_mask_init();

	fpstate_xstate_init_size();
	fpu__init_system_xstate();

	fpu__init_system_ctx_switch();
}

static int __init no_387(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_FPU);
	return 1;
}

__setup("no387", no_387);

/*
 * Set the X86_FEATURE_FPU CPU-capability bit based on
 * trying to execute an actual sequence of FPU instructions:
 */
void fpu__detect(struct cpuinfo_x86 *c)
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

	fpu__init_system();
	/* The final cr0 value is set later, in fpu_init() */
}
