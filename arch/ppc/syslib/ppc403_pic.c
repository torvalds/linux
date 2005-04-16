/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: ppc403_pic.c
 *
 *    Description:
 *      Interrupt controller driver for PowerPC 403-based processors.
 */

/*
 * The PowerPC 403 cores' Asynchronous Interrupt Controller (AIC) has
 * 32 possible interrupts, a majority of which are not implemented on
 * all cores. There are six configurable, external interrupt pins and
 * there are eight internal interrupts for the on-chip serial port
 * (SPU), DMA controller, and JTAG controller.
 *
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/stddef.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/ppc4xx_pic.h>

/* Function Prototypes */

static void ppc403_aic_enable(unsigned int irq);
static void ppc403_aic_disable(unsigned int irq);
static void ppc403_aic_disable_and_ack(unsigned int irq);

static struct hw_interrupt_type ppc403_aic = {
	"403GC AIC",
	NULL,
	NULL,
	ppc403_aic_enable,
	ppc403_aic_disable,
	ppc403_aic_disable_and_ack,
	0
};

int
ppc403_pic_get_irq(struct pt_regs *regs)
{
	int irq;
	unsigned long bits;

	/*
	 * Only report the status of those interrupts that are actually
	 * enabled.
	 */

	bits = mfdcr(DCRN_EXISR) & mfdcr(DCRN_EXIER);

	/*
	 * Walk through the interrupts from highest priority to lowest, and
	 * report the first pending interrupt found.
	 * We want PPC, not C bit numbering, so just subtract the ffs()
	 * result from 32.
	 */
	irq = 32 - ffs(bits);

	if (irq == NR_AIC_IRQS)
		irq = -1;

	return (irq);
}

static void
ppc403_aic_enable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
}

static void
ppc403_aic_disable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
}

static void
ppc403_aic_disable_and_ack(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
	mtdcr(DCRN_EXISR, (1 << (31 - bit)));
}

void __init
ppc4xx_pic_init(void)
{
	int i;

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	ppc_cached_irq_mask[0] = 0;

	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[0]);

	ppc_md.get_irq = ppc403_pic_get_irq;

	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].handler = &ppc403_aic;
}
