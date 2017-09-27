#ifndef _ASM_POWERPC_IRQ_WORK_H
#define _ASM_POWERPC_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
	return true;
}

#endif /* _ASM_POWERPC_IRQ_WORK_H */
