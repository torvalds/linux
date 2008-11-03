#ifndef _ASM_ARCH_IRQ_H
#define _ASM_ARCH_IRQ_H

#include <hwregs/intr_vect.h>

/* Number of non-cpu interrupts. */
#define NR_IRQS NBR_INTR_VECT /* Exceptions + IRQs */
#define FIRST_IRQ 0x31 /* Exception number for first IRQ */
#define NR_REAL_IRQS (NBR_INTR_VECT - FIRST_IRQ) /* IRQs */
#if NR_REAL_IRQS > 32
#define MACH_IRQS 64
#else
#define MACH_IRQS 32
#endif

#ifndef __ASSEMBLY__
/* Global IRQ vector. */
typedef void (*irqvectptr)(void);

struct etrax_interrupt_vector {
	irqvectptr v[256];
};

extern struct etrax_interrupt_vector *etrax_irv;	/* head.S */

void mask_irq(int irq);
void unmask_irq(int irq);

void set_exception_vector(int n, irqvectptr addr);

/* Save registers so that they match pt_regs. */
#define SAVE_ALL \
	"subq 12,$sp\n\t"	\
	"move $erp,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move $srp,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move $ccs,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move $spc,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move $mof,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move $srs,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move.d $acr,[$sp]\n\t"	\
	"subq 14*4,$sp\n\t"	\
	"movem $r13,[$sp]\n\t"	\
	"subq 4,$sp\n\t"	\
	"move.d $r10,[$sp]\n"

#define STR2(x) #x
#define STR(x) STR2(x)

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

/*
 * The reason for setting the S-bit when debugging the kernel is that we want
 * hardware breakpoints to remain active while we are in an exception handler.
 * Note that we cannot simply copy S1, since we may come here from user-space,
 * or any context where the S-bit wasn't set.
 */
#ifdef CONFIG_ETRAX_KGDB
#define KGDB_FIXUP \
	"move $ccs, $r10\n\t"		\
	"or.d (1<<9), $r10\n\t"		\
	"move $r10, $ccs\n\t"
#else
#define KGDB_FIXUP ""
#endif

/*
 * Make sure the causing IRQ is blocked, then call do_IRQ. After that, unblock
 * and jump to ret_from_intr which is found in entry.S.
 *
 * The reason for blocking the IRQ is to allow an sti() before the handler,
 * which will acknowledge the interrupt, is run. The actual blocking is made
 * by crisv32_do_IRQ.
 */
#define BUILD_IRQ(nr)		        \
void IRQ_NAME(nr);			\
__asm__ (				\
	".text\n\t"			\
	"IRQ" #nr "_interrupt:\n\t" 	\
	SAVE_ALL			\
	KGDB_FIXUP                      \
	"move.d "#nr",$r10\n\t"		\
	"move.d $sp, $r12\n\t"          \
	"jsr crisv32_do_IRQ\n\t"       	\
	"moveq 1, $r11\n\t"		\
	"jump ret_from_intr\n\t"	\
	"nop\n\t");
/*
 * This is subtle. The timer interrupt is crucial and it should not be disabled
 * for too long. However, if it had been a normal interrupt as per BUILD_IRQ, it
 * would have been BLOCK'ed, and then softirq's are run before we return here to
 * UNBLOCK. If the softirq's take too much time to run, the timer irq won't run
 * and the watchdog will kill us.
 *
 * Furthermore, if a lot of other irq's occur before we return here, the
 * multiple_irq handler is run and it prioritizes the timer interrupt. However
 * if we had BLOCK'edit here, we would not get the multiple_irq at all.
 *
 * The non-blocking here is based on the knowledge that the timer interrupt is
 * registred as a fast interrupt (IRQF_DISABLED) so that we _know_ there will not
 * be an sti() before the timer irq handler is run to acknowledge the interrupt.
 */
#define BUILD_TIMER_IRQ(nr, mask) 	\
void IRQ_NAME(nr);			\
__asm__ (				\
	".text\n\t"			\
	"IRQ" #nr "_interrupt:\n\t"	\
	SAVE_ALL			\
        KGDB_FIXUP                      \
	"move.d "#nr",$r10\n\t"		\
	"move.d $sp,$r12\n\t"		\
	"jsr crisv32_do_IRQ\n\t"	\
	"moveq 0,$r11\n\t"		\
	"jump ret_from_intr\n\t"	\
	"nop\n\t");

#endif /* __ASSEMBLY__ */
#endif /* _ASM_ARCH_IRQ_H */
