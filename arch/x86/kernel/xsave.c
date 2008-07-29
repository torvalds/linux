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

#ifdef CONFIG_X86_64
/*
 * Signal frame handlers.
 */

int save_i387_xstate(void __user *buf)
{
	struct task_struct *tsk = current;
	int err = 0;

	if (!access_ok(VERIFY_WRITE, buf, sig_xstate_size))
		return -EACCES;

	BUILD_BUG_ON(sizeof(struct user_i387_struct) !=
			sizeof(tsk->thread.xstate->fxsave));

	if ((unsigned long)buf % 16)
		printk("save_i387_xstate: bad fpstate %p\n", buf);

	if (!used_math())
		return 0;
	clear_used_math(); /* trigger finit */
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		err = save_i387_checking((struct i387_fxsave_struct __user *)
					 buf);
		if (err)
			return err;
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		stts();
	} else {
		if (__copy_to_user(buf, &tsk->thread.xstate->fxsave,
				   xstate_size))
			return -1;
	}
	return 1;
}

/*
 * This restores directly out of user space. Exceptions are handled.
 */
int restore_i387_xstate(void __user *buf)
{
	struct task_struct *tsk = current;
	int err;

	if (!buf) {
		if (used_math()) {
			clear_fpu(tsk);
			clear_used_math();
		}

		return 0;
	} else
		if (!access_ok(VERIFY_READ, buf, sig_xstate_size))
			return -EACCES;

	if (!used_math()) {
		err = init_fpu(tsk);
		if (err)
			return err;
	}

	if (!(task_thread_info(current)->status & TS_USEDFPU)) {
		clts();
		task_thread_info(current)->status |= TS_USEDFPU;
	}
	err = fxrstor_checking((__force struct i387_fxsave_struct *)buf);
	if (unlikely(err)) {
		/*
		 * Encountered an error while doing the restore from the
		 * user buffer, clear the fpu state.
		 */
		clear_fpu(tsk);
		clear_used_math();
	}
	return err;
}
#endif

/*
 * Represents init state for the supported extended state.
 */
struct xsave_struct *init_xstate_buf;

#ifdef CONFIG_X86_64
unsigned int sig_xstate_size = sizeof(struct _fpstate);
#endif

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
