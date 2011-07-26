#ifndef __ASM_CRIS_ARCH_PROCESSOR_H
#define __ASM_CRIS_ARCH_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({void *pc; __asm__ ("move.d $pc,%0" : "=rm" (pc)); pc; })

/* CRIS has no problems with write protection */
#define wp_works_ok 1

/* CRIS thread_struct. this really has nothing to do with the processor itself, since
 * CRIS does not do any hardware task-switching, but it's here for legacy reasons.
 * The thread_struct here is used when task-switching using _resume defined in entry.S.
 * The offsets here are hardcoded into _resume - if you change this struct, you need to
 * change them as well!!!
*/

struct thread_struct {
	unsigned long ksp;     /* kernel stack pointer */
	unsigned long usp;     /* user stack pointer */
	unsigned long dccr;    /* saved flag register */
};

/*
 * User space process size. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */

#ifdef CONFIG_CRIS_LOW_MAP
#define TASK_SIZE       (0x50000000UL)   /* 1.25 GB */
#else
#define TASK_SIZE       (0xA0000000UL)   /* 2.56 GB */
#endif

#define INIT_THREAD  { \
   0, 0, 0x20 }  /* ccr = int enable, nothing else */

#define KSTK_EIP(tsk)	\
({			\
	unsigned long eip = 0;   \
	unsigned long regs = (unsigned long)task_pt_regs(tsk); \
	if (regs > PAGE_SIZE && \
		virt_addr_valid(regs)) \
	eip = ((struct pt_regs *)regs)->irp; \
	eip; \
})

/* give the thread a program location
 * set user-mode (The 'U' flag (User mode flag) is CCR/DCCR bit 8) 
 * switch user-stackpointer
 */

#define start_thread(regs, ip, usp) do { \
	regs->irp = ip;       \
	regs->dccr |= 1 << U_DCCR_BITNR; \
	wrusp(usp);           \
} while(0)

/* Called when handling a kernel bus fault fixup.
 *
 * After a fixup we do not want to return by restoring the CPU-state
 * anymore, so switch frame-types (see ptrace.h)
 */
#define arch_fixup(regs) \
   regs->frametype = CRIS_FRAME_NORMAL;

#endif
