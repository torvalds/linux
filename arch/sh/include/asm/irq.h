#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

#include <asm/machvec.h>

/*
 * A sane default based on a reasonable vector table size, platforms are
 * advised to cap this at the hard limit that they're interested in
 * through the machvec.
 */
#define NR_IRQS			256
#define NR_IRQS_LEGACY		8	/* Legacy external IRQ0-7 */

/*
 * Convert back and forth between INTEVT and IRQ values.
 */
#ifdef CONFIG_CPU_HAS_INTEVT
#define evt2irq(evt)		(((evt) >> 5) - 16)
#define irq2evt(irq)		(((irq) + 16) << 5)
#else
#define evt2irq(evt)		(evt)
#define irq2evt(irq)		(irq)
#endif

/*
 * Simple Mask Register Support
 */
extern void make_maskreg_irq(unsigned int irq);
extern unsigned short *irq_mask_register;

/*
 * PINT IRQs
 */
void init_IRQ_pint(void);
void make_imask_irq(unsigned int irq);

static inline int generic_irq_demux(int irq)
{
	return irq;
}

#define irq_canonicalize(irq)	(irq)
#define irq_demux(irq)		sh_mv.mv_irq_demux(irq)

void init_IRQ(void);
asmlinkage int do_IRQ(unsigned int irq, struct pt_regs *regs);

#ifdef CONFIG_IRQSTACKS
extern void irq_ctx_init(int cpu);
extern void irq_ctx_exit(int cpu);
# define __ARCH_HAS_DO_SOFTIRQ
#else
# define irq_ctx_init(cpu) do { } while (0)
# define irq_ctx_exit(cpu) do { } while (0)
#endif

#ifdef CONFIG_CPU_SH5
#include <cpu/irq.h>
#endif

#endif /* __ASM_SH_IRQ_H */
