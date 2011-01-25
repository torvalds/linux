/*
 * Copyright 2010 PMC-Sierra, Inc, derived from irq_cpu.c
 *
 * This file define the irq handler for MSP PER subsystem interrupts.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include <asm/mipsregs.h>
#include <asm/system.h>

#include <msp_cic_int.h>
#include <msp_regs.h>


/*
 * Convenience Macro.  Should be somewhere generic.
 */
#define get_current_vpe()	\
	((read_c0_tcbind() >> TCBIND_CURVPE_SHIFT) & TCBIND_CURVPE)

#ifdef CONFIG_SMP
/*
 * The PER registers must be protected from concurrent access.
 */

static DEFINE_SPINLOCK(per_lock);
#endif

/* ensure writes to per are completed */

static inline void per_wmb(void)
{
	const volatile void __iomem *per_mem = PER_INT_MSK_REG;
	volatile u32 dummy_read;

	wmb();
	dummy_read = __raw_readl(per_mem);
	dummy_read++;
}

static inline void unmask_per_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	spin_lock_irqsave(&per_lock, flags);
	*PER_INT_MSK_REG |= (1 << (irq - MSP_PER_INTBASE));
	spin_unlock_irqrestore(&per_lock, flags);
#else
	*PER_INT_MSK_REG |= (1 << (irq - MSP_PER_INTBASE));
#endif
	per_wmb();
}

static inline void mask_per_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	spin_lock_irqsave(&per_lock, flags);
	*PER_INT_MSK_REG &= ~(1 << (irq - MSP_PER_INTBASE));
	spin_unlock_irqrestore(&per_lock, flags);
#else
	*PER_INT_MSK_REG &= ~(1 << (irq - MSP_PER_INTBASE));
#endif
	per_wmb();
}

static inline void msp_per_irq_enable(unsigned int irq)
{
	unmask_per_irq(irq);
}

static inline void msp_per_irq_disable(unsigned int irq)
{
	 mask_per_irq(irq);
}

static unsigned int msp_per_irq_startup(unsigned int irq)
{
	msp_per_irq_enable(irq);
	return 0;
}

#define    msp_per_irq_shutdown    msp_per_irq_disable

static inline void msp_per_irq_ack(unsigned int irq)
{
	mask_per_irq(irq);
	/*
	 * In the PER interrupt controller, only bits 11 and 10
	 * are write-to-clear, (SPI TX complete, SPI RX complete).
	 * It does nothing for any others.
	 */

	*PER_INT_STS_REG = (1 << (irq - MSP_PER_INTBASE));

	/* Re-enable the CIC cascaded interrupt and return */
	irq_desc[MSP_INT_CIC].chip->end(MSP_INT_CIC);
}

static void msp_per_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		unmask_per_irq(irq);
}

#ifdef CONFIG_SMP
static inline int msp_per_irq_set_affinity(unsigned int irq,
				const struct cpumask *affinity)
{
	unsigned long flags;
	/*
	 * Calls to ack, end, startup, enable are spinlocked in setup_irq and
	 * __do_IRQ.Callers of this function do not spinlock,so we need to
	 * do so ourselves.
	 */
	raw_spin_lock_irqsave(&irq_desc[irq].lock, flags);
	msp_per_irq_enable(irq);
	raw_spin_unlock_irqrestore(&irq_desc[irq].lock, flags);
	return 0;

}
#endif

static struct irq_chip msp_per_irq_controller = {
	.name = "MSP_PER",
	.startup = msp_per_irq_startup,
	.shutdown = msp_per_irq_shutdown,
	.enable = msp_per_irq_enable,
	.disable = msp_per_irq_disable,
#ifdef CONFIG_SMP
	.set_affinity = msp_per_irq_set_affinity,
#endif
	.ack = msp_per_irq_ack,
	.end = msp_per_irq_end,
};

void __init msp_per_irq_init(void)
{
	int i;
	/* Mask/clear interrupts. */
	*PER_INT_MSK_REG  = 0x00000000;
	*PER_INT_STS_REG  = 0xFFFFFFFF;
	/* initialize all the IRQ descriptors */
	for (i = MSP_PER_INTBASE; i < MSP_PER_INTBASE + 32; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].chip = &msp_per_irq_controller;
#ifdef CONFIG_MIPS_MT_SMTC
		irq_hwmask[i] = C_IRQ4;
#endif
	}
}

void msp_per_irq_dispatch(void)
{
	u32	per_mask = *PER_INT_MSK_REG;
	u32	per_status = *PER_INT_STS_REG;
	u32	pending;

	pending = per_status & per_mask;
	if (pending) {
		do_IRQ(ffs(pending) + MSP_PER_INTBASE - 1);
	} else {
		spurious_interrupt();
	/* Re-enable the CIC cascaded interrupt and return */
	irq_desc[MSP_INT_CIC].chip->end(MSP_INT_CIC);
	}
}
