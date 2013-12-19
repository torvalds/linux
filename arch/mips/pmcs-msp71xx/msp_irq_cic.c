/*
 * Copyright 2010 PMC-Sierra, Inc, derived from irq_cpu.c
 *
 * This file define the irq handler for MSP CIC subsystem interrupts.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/irq.h>

#include <asm/mipsregs.h>

#include <msp_cic_int.h>
#include <msp_regs.h>

/*
 * External API
 */
extern void msp_per_irq_init(void);
extern void msp_per_irq_dispatch(void);


/*
 * Convenience Macro.  Should be somewhere generic.
 */
#define get_current_vpe()   \
	((read_c0_tcbind() >> TCBIND_CURVPE_SHIFT) & TCBIND_CURVPE)

#ifdef CONFIG_SMP

#define LOCK_VPE(flags, mtflags) \
do {				\
	local_irq_save(flags);	\
	mtflags = dmt();	\
} while (0)

#define UNLOCK_VPE(flags, mtflags) \
do {				\
	emt(mtflags);		\
	local_irq_restore(flags);\
} while (0)

#define LOCK_CORE(flags, mtflags) \
do {				\
	local_irq_save(flags);	\
	mtflags = dvpe();	\
} while (0)

#define UNLOCK_CORE(flags, mtflags)		\
do {				\
	evpe(mtflags);		\
	local_irq_restore(flags);\
} while (0)

#else

#define LOCK_VPE(flags, mtflags)
#define UNLOCK_VPE(flags, mtflags)
#endif

/* ensure writes to cic are completed */
static inline void cic_wmb(void)
{
	const volatile void __iomem *cic_mem = CIC_VPE0_MSK_REG;
	volatile u32 dummy_read;

	wmb();
	dummy_read = __raw_readl(cic_mem);
	dummy_read++;
}

static void unmask_cic_irq(struct irq_data *d)
{
	volatile u32   *cic_msk_reg = CIC_VPE0_MSK_REG;
	int vpe;
#ifdef CONFIG_SMP
	unsigned int mtflags;
	unsigned long  flags;

	/*
	* Make sure we have IRQ affinity.  It may have changed while
	* we were processing the IRQ.
	*/
	if (!cpumask_test_cpu(smp_processor_id(), d->affinity))
		return;
#endif

	vpe = get_current_vpe();
	LOCK_VPE(flags, mtflags);
	cic_msk_reg[vpe] |= (1 << (d->irq - MSP_CIC_INTBASE));
	UNLOCK_VPE(flags, mtflags);
	cic_wmb();
}

static void mask_cic_irq(struct irq_data *d)
{
	volatile u32 *cic_msk_reg = CIC_VPE0_MSK_REG;
	int	vpe = get_current_vpe();
#ifdef CONFIG_SMP
	unsigned long flags, mtflags;
#endif
	LOCK_VPE(flags, mtflags);
	cic_msk_reg[vpe] &= ~(1 << (d->irq - MSP_CIC_INTBASE));
	UNLOCK_VPE(flags, mtflags);
	cic_wmb();
}
static void msp_cic_irq_ack(struct irq_data *d)
{
	mask_cic_irq(d);
	/*
	* Only really necessary for 18, 16-14 and sometimes 3:0
	* (since these can be edge sensitive) but it doesn't
	* hurt for the others
	*/
	*CIC_STS_REG = (1 << (d->irq - MSP_CIC_INTBASE));
	smtc_im_ack_irq(d->irq);
}

/*Note: Limiting to VSMP . Not tested in SMTC */

#ifdef CONFIG_MIPS_MT_SMP
static int msp_cic_irq_set_affinity(struct irq_data *d,
				    const struct cpumask *cpumask, bool force)
{
	int cpu;
	unsigned long flags;
	unsigned int  mtflags;
	unsigned long imask = (1 << (irq - MSP_CIC_INTBASE));
	volatile u32 *cic_mask = (volatile u32 *)CIC_VPE0_MSK_REG;

	/* timer balancing should be disabled in kernel code */
	BUG_ON(irq == MSP_INT_VPE0_TIMER || irq == MSP_INT_VPE1_TIMER);

	LOCK_CORE(flags, mtflags);
	/* enable if any of each VPE's TCs require this IRQ */
	for_each_online_cpu(cpu) {
		if (cpumask_test_cpu(cpu, cpumask))
			cic_mask[cpu] |= imask;
		else
			cic_mask[cpu] &= ~imask;

	}

	UNLOCK_CORE(flags, mtflags);
	return 0;

}
#endif

static struct irq_chip msp_cic_irq_controller = {
	.name = "MSP_CIC",
	.irq_mask = mask_cic_irq,
	.irq_mask_ack = msp_cic_irq_ack,
	.irq_unmask = unmask_cic_irq,
	.irq_ack = msp_cic_irq_ack,
#ifdef CONFIG_MIPS_MT_SMP
	.irq_set_affinity = msp_cic_irq_set_affinity,
#endif
};

void __init msp_cic_irq_init(void)
{
	int i;
	/* Mask/clear interrupts. */
	*CIC_VPE0_MSK_REG = 0x00000000;
	*CIC_VPE1_MSK_REG = 0x00000000;
	*CIC_STS_REG	  = 0xFFFFFFFF;
	/*
	* The MSP7120 RG and EVBD boards use IRQ[6:4] for PCI.
	* These inputs map to EXT_INT_POL[6:4] inside the CIC.
	* They are to be active low, level sensitive.
	*/
	*CIC_EXT_CFG_REG &= 0xFFFF8F8F;

	/* initialize all the IRQ descriptors */
	for (i = MSP_CIC_INTBASE ; i < MSP_CIC_INTBASE + 32 ; i++) {
		irq_set_chip_and_handler(i, &msp_cic_irq_controller,
					 handle_level_irq);
#ifdef CONFIG_MIPS_MT_SMTC
		/* Mask of CIC interrupt */
		irq_hwmask[i] = C_IRQ4;
#endif
	}

	/* Initialize the PER interrupt sub-system */
	 msp_per_irq_init();
}

/* CIC masked by CIC vector processing before dispatch called */
void msp_cic_irq_dispatch(void)
{
	volatile u32	*cic_msk_reg = (volatile u32 *)CIC_VPE0_MSK_REG;
	u32	cic_mask;
	u32	 pending;
	int	cic_status = *CIC_STS_REG;
	cic_mask = cic_msk_reg[get_current_vpe()];
	pending = cic_status & cic_mask;
	if (pending & (1 << (MSP_INT_VPE0_TIMER - MSP_CIC_INTBASE))) {
		do_IRQ(MSP_INT_VPE0_TIMER);
	} else if (pending & (1 << (MSP_INT_VPE1_TIMER - MSP_CIC_INTBASE))) {
		do_IRQ(MSP_INT_VPE1_TIMER);
	} else if (pending & (1 << (MSP_INT_PER - MSP_CIC_INTBASE))) {
		msp_per_irq_dispatch();
	} else if (pending) {
		do_IRQ(ffs(pending) + MSP_CIC_INTBASE - 1);
	} else{
		spurious_interrupt();
	}
}
