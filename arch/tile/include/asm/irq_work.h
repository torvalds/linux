#ifndef __ASM_IRQ_WORK_H
#define __ASM_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
#ifdef CONFIG_SMP
	extern bool self_interrupt_ok;
	return self_interrupt_ok;
#else
	return false;
#endif
}

#endif /* __ASM_IRQ_WORK_H */
