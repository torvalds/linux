/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SUSPEND_H
#define __ASM_SUSPEND_H

#define NR_CTX_REGS 12
#define NR_CALLEE_SAVED_REGS 12

/*
 * struct cpu_suspend_ctx must be 16-byte aligned since it is allocated on
 * the stack, which must be 16-byte aligned on v8
 */
struct cpu_suspend_ctx {
	/*
	 * This struct must be kept in sync with
	 * cpu_do_{suspend/resume} in mm/proc.S
	 */
	u64 ctx_regs[NR_CTX_REGS];
	u64 sp;
} __aligned(16);

/*
 * Memory to save the cpu state is allocated on the stack by
 * __cpu_suspend_enter()'s caller, and populated by __cpu_suspend_enter().
 * This data must survive until cpu_resume() is called.
 *
 * This struct desribes the size and the layout of the saved cpu state.
 * The layout of the callee_saved_regs is defined by the implementation
 * of __cpu_suspend_enter(), and cpu_resume(). This struct must be passed
 * in by the caller as __cpu_suspend_enter()'s stack-frame is gone once it
 * returns, and the data would be subsequently corrupted by the call to the
 * finisher.
 */
struct sleep_stack_data {
	struct cpu_suspend_ctx	system_regs;
	unsigned long		callee_saved_regs[NR_CALLEE_SAVED_REGS];
};

extern unsigned long *sleep_save_stash;

extern int cpu_suspend(unsigned long arg, int (*fn)(unsigned long));
extern void cpu_resume(void);
int __cpu_suspend_enter(struct sleep_stack_data *state);
void __cpu_suspend_exit(void);
void _cpu_resume(void);

int swsusp_arch_suspend(void);
int swsusp_arch_resume(void);
int arch_hibernation_header_save(void *addr, unsigned int max_size);
int arch_hibernation_header_restore(void *addr);

/* Used to resume on the CPU we hibernated on */
int hibernate_resume_nonboot_cpu_disable(void);

#endif
