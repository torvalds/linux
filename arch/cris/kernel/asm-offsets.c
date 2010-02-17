#include <linux/sched.h>
#include <asm/thread_info.h>

/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

#if !defined(CONFIG_ETRAX_ARCH_V10) && !defined(CONFIG_ETRAX_ARCH_V32)
#error One of ARCH v10 and ARCH v32 must be true!
#endif

int main(void)
{
#define ENTRY(entry) DEFINE(PT_ ## entry, offsetof(struct pt_regs, entry))
	ENTRY(orig_r10);
	ENTRY(r13);
	ENTRY(r12);
	ENTRY(r11);
	ENTRY(r10);
	ENTRY(r9);
#ifdef CONFIG_ETRAX_ARCH_V32
	ENTRY(acr);
	ENTRY(srs);
#endif
	ENTRY(mof);
#ifdef CONFIG_ETRAX_ARCH_V10
	ENTRY(dccr);
#else
	ENTRY(ccs);
#endif
	ENTRY(srp);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(TI_ ## entry, offsetof(struct thread_info, entry))
	ENTRY(task);
	ENTRY(flags);
	ENTRY(preempt_count);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(THREAD_ ## entry, offsetof(struct thread_struct, entry))
	ENTRY(ksp);
	ENTRY(usp);
#ifdef CONFIG_ETRAX_ARCH_V10
	ENTRY(dccr);
#else
	ENTRY(ccs);
#endif
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(TASK_ ## entry, offsetof(struct task_struct, entry))
	ENTRY(pid);
	BLANK();
	DEFINE(LCLONE_VM, CLONE_VM);
	DEFINE(LCLONE_UNTRACED, CLONE_UNTRACED);
	return 0;
}
