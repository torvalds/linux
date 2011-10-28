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

static inline void unmask_per_irq(struct irq_data *d)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	spin_lock_irqsave(&per_lock, flags);
	*PER_INT_MSK_REG |= (1 << (d->irq - MSP_PER_INTBASE));
	spin_unlock_irqrestore(&per_lock, flags);
#else
	*PER_INT_MSK_REG |= (1 << (d->irq - MSP_PER_INTBASE));
#endif
	per_wmb();
}

static inline void mask_per_irq(struct irq_data *d)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	spin_lock_irqsave(&per_lock, flags);
	*PER_INT_MSK_REG &= ~(1 << (d->irq - MSP_PER_INTBASE));
	spin_unlock_irqrestore(&per_lock, flags);
#else
	*PER_INT_MSK_REG &= ~(1 << (d->irq - MSP_PER_INTBASE));
#endif
	per_wmb();
}

static inline void msp_per_irq_ack(struct irq_data *d)
{
	mask_per_irq(d);
	/*
	 * In the PER interrupt controller, only bits 11 and 10
	 * are write-to-clear, (SPI TX complete, SPI RX complete).
	 * It does nothing for any others.
	 */
	*PER_INT_STS_REG = (1 << (d->irq - MSP_PER_INTBASE));
}

#ifdef CONFIG_SMP
static int msp_per_irq_set_affinity(struct irq_data *d,
				    const struct cpumask *affinity, bool force)
{
	/* WTF is this doing ????? */
	unmask_per_irq(d);
	return 0;
}
#endif

static struct irq_chip msp_per_irq_controller = {
	.name = "MSP_PER",
	.irq_enable = unmask_per_irq,
	.irq_disable = mask_per_irq,
	.irq_ack = msp_per_irq_ack,
#ifdef CONFIG_SMP
	.irq_set_affinity = msp_per_irq_set_affinity,
#endif
};

void __init msp_per_irq_init(void)
{
	int i;
	/* Mask/clear interrupts. */
	*PER_INT_MSK_REG  = 0x00000000;
	*PER_INT_STS_REG  = 0xFFFFFFFF;
	/* initialize all the IRQ descriptors */
	for (i = MSP_PER_INTBASE; i < MSP_PER_INTBASE + 32; i++) {
		irq_set_chip(i, &msp_per_irq_controller);
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
	}
}
