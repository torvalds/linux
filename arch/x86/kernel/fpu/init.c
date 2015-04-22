/*
 * x86 FPU boot time init code
 */
#include <asm/fpu-internal.h>
#include <asm/tlbflush.h>

unsigned int mxcsr_feature_mask __read_mostly = 0xffffffffu;
unsigned int xstate_size;
EXPORT_SYMBOL_GPL(xstate_size);
static struct i387_fxsave_struct fx_scratch;

static void mxcsr_feature_mask_init(void)
{
	unsigned long mask = 0;

	if (cpu_has_fxsr) {
		memset(&fx_scratch, 0, sizeof(struct i387_fxsave_struct));
		asm volatile("fxsave %0" : "+m" (fx_scratch));
		mask = fx_scratch.mxcsr_mask;
		if (mask == 0)
			mask = 0x0000ffbf;
	}
	mxcsr_feature_mask &= mask;
}

static void fpstate_xstate_init_size(void)
{
	/*
	 * Note that xstate_size might be overwriten later during
	 * xsave_init().
	 */

	if (!cpu_has_fpu) {
		/*
		 * Disable xsave as we do not support it if i387
		 * emulation is enabled.
		 */
		setup_clear_cpu_cap(X86_FEATURE_XSAVE);
		setup_clear_cpu_cap(X86_FEATURE_XSAVEOPT);
		xstate_size = sizeof(struct i387_soft_struct);
		return;
	}

	if (cpu_has_fxsr)
		xstate_size = sizeof(struct i387_fxsave_struct);
	else
		xstate_size = sizeof(struct i387_fsave_struct);
}

/*
 * Called on the boot CPU at bootup to set up the initial FPU state that
 * is later cloned into all processes.
 *
 * Also called on secondary CPUs to set up the FPU state of their
 * idle threads.
 */
void fpu__cpu_init(void)
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

	/*
	 * fpstate_xstate_init_size() is only called once, to avoid overriding
	 * 'xstate_size' during (secondary CPU) bootup or during CPU hotplug.
	 */
	if (xstate_size == 0)
		fpstate_xstate_init_size();

	mxcsr_feature_mask_init();
	xsave_init();
	eager_fpu_init();
}
