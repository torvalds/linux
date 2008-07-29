/*
 * xsave/xrstor support.
 *
 * Author: Suresh Siddha <suresh.b.siddha@intel.com>
 */
#include <linux/bootmem.h>
#include <linux/compat.h>
#include <asm/i387.h>

/*
 * Supported feature mask by the CPU and the kernel.
 */
unsigned int pcntxt_hmask, pcntxt_lmask;

/*
 * Represents init state for the supported extended state.
 */
struct xsave_struct *init_xstate_buf;

/*
 * Enable the extended processor state save/restore feature
 */
void __cpuinit xsave_init(void)
{
	if (!cpu_has_xsave)
		return;

	set_in_cr4(X86_CR4_OSXSAVE);

	/*
	 * Enable all the features that the HW is capable of
	 * and the Linux kernel is aware of.
	 *
	 * xsetbv();
	 */
	asm volatile(".byte 0x0f,0x01,0xd1" : : "c" (0),
		     "a" (pcntxt_lmask), "d" (pcntxt_hmask));
}

/*
 * setup the xstate image representing the init state
 */
void setup_xstate_init(void)
{
	init_xstate_buf = alloc_bootmem(xstate_size);
	init_xstate_buf->i387.mxcsr = MXCSR_DEFAULT;
}

/*
 * Enable and initialize the xsave feature.
 */
void __init xsave_cntxt_init(void)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid_count(0xd, 0, &eax, &ebx, &ecx, &edx);

	pcntxt_lmask = eax;
	pcntxt_hmask = edx;

	if ((pcntxt_lmask & XSTATE_FPSSE) != XSTATE_FPSSE) {
		printk(KERN_ERR "FP/SSE not shown under xsave features %x\n",
		       pcntxt_lmask);
		BUG();
	}

	/*
	 * for now OS knows only about FP/SSE
	 */
	pcntxt_lmask = pcntxt_lmask & XCNTXT_LMASK;
	pcntxt_hmask = pcntxt_hmask & XCNTXT_HMASK;

	xsave_init();

	/*
	 * Recompute the context size for enabled features
	 */
	cpuid_count(0xd, 0, &eax, &ebx, &ecx, &edx);

	xstate_size = ebx;

	setup_xstate_init();

	printk(KERN_INFO "xsave/xrstor: enabled xstate_bv 0x%Lx, "
	       "cntxt size 0x%x\n",
	       (pcntxt_lmask | ((u64) pcntxt_hmask << 32)), xstate_size);
}
