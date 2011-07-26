#ifndef _ASM_CRIS_ARCH_PROCESSOR_H
#define _ASM_CRIS_ARCH_PROCESSOR_H


/* Return current instruction pointer. */
#define current_text_addr() \
	({void *pc; __asm__ __volatile__ ("lapcq .,%0" : "=rm" (pc)); pc;})

/*
 * Since CRIS doesn't do hardware task-switching this hasn't really anything to
 * do with the proccessor itself, it's just here for legacy reasons. This is
 * used when task-switching using _resume defined in entry.S. The offsets here
 * are hardcoded into _resume, so if this struct is changed, entry.S needs to be
 * changed as well.
 */
struct thread_struct {
	unsigned long ksp;	/* Kernel stack pointer. */
	unsigned long usp;	/* User stack pointer. */
	unsigned long ccs;	/* Saved flags register. */
};

/*
 * User-space process size. This is hardcoded into a few places, so don't
 * changed it unless everything's clear!
 */
#ifndef CONFIG_ETRAX_VCS_SIM
#define TASK_SIZE	(0xB0000000UL)
#else
#define TASK_SIZE	(0xA0000000UL)
#endif

/* CCS I=1, enable interrupts. */
#define INIT_THREAD { 0, 0, (1 << I_CCS_BITNR) }

#define KSTK_EIP(tsk)		\
({				\
	unsigned long eip = 0;	\
	unsigned long regs = (unsigned long)task_pt_regs(tsk); \
	if (regs > PAGE_SIZE && virt_addr_valid(regs))	    \
		eip = ((struct pt_regs *)regs)->erp;	    \
	eip; \
})

/*
 * Give the thread a program location, set user-mode and switch user
 * stackpointer.
 */
#define start_thread(regs, ip, usp) \
do { \
	regs->erp = ip; \
	regs->ccs |= 1 << (U_CCS_BITNR + CCS_SHIFT); \
	wrusp(usp); \
} while(0)

/* Nothing special to do for v32 when handling a kernel bus fault fixup. */
#define arch_fixup(regs) {};

#endif /* _ASM_CRIS_ARCH_PROCESSOR_H */
