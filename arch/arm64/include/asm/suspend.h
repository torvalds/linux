#ifndef __ASM_SUSPEND_H
#define __ASM_SUSPEND_H

#define NR_CTX_REGS 11

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

struct sleep_save_sp {
	phys_addr_t *save_ptr_stash;
	phys_addr_t save_ptr_stash_phys;
};

extern void cpu_resume(void);
extern int cpu_suspend(unsigned long);

#endif
