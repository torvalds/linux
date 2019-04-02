// SPDX-License-Identifier: GPL-2.0
/*
 * x86 FPU  checks:
 */
#include <asm/fpu/internal.h>

/*
 * Boot time CPU/FPU FDIV  detection code:
 */

static double __initdata x = 4195835.0;
static double __initdata y = 3145727.0;

/*
 * This used to check for exceptions..
 * However, it turns out that to support that,
 * the XMM trap handlers basically had to
 * be gy. So let's have a correct XMM trap
 * handler, and forget about printing out
 * some status at boot.
 *
 * We should really only care about s here
 * anyway. Not features.
 */
void __init fpu__init_check_s(void)
{
	s32 fdiv_;

	/* kernel_fpu_begin/end() relies on patched alternative instructions. */
	if (!boot_cpu_has(X86_FEATURE_FPU))
		return;

	kernel_fpu_begin();

	/*
	 * trap_init() enabled FXSR and company _before_ testing for FP
	 * problems here.
	 *
	 * Test for the divl : http://en.wikipedia.org/wiki/Fdiv_
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
		: "=m" (*&fdiv_)
		: "m" (*&x), "m" (*&y));

	kernel_fpu_end();

	if (fdiv_) {
		set_cpu_(&boot_cpu_data, X86__FDIV);
		pr_warn("Hmm, FPU with FDIV \n");
	}
}
