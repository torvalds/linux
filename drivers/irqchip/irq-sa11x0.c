// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Dmitry Eremin-Solenikov
 * Copyright (C) 1999-2001 Nicolas Pitre
 *
 * Generic IRQ handling for the SA11x0.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/syscore_ops.h>
#include <linux/irqchip/irq-sa11x0.h>

#include <soc/sa1100/pwer.h>

#include <asm/exception.h>

#define ICIP	0x00  /* IC IRQ Pending reg. */
#define ICMR	0x04  /* IC Mask Reg.        */
#define ICLR	0x08  /* IC Level Reg.       */
#define ICCR	0x0C  /* IC Control Reg.     */
#define ICFP	0x10  /* IC FIQ Pending reg. */
#define ICPR	0x20  /* IC Pending Reg.     */

static void __iomem *iobase;

/*
 * We don't need to ACK IRQs on the SA1100 unless they're GPIOs
 * this is for internal IRQs i.e. from IRQ LCD to RTCAlrm.
 */
static void sa1100_mask_irq(struct irq_data *d)
{
	u32 reg;

	reg = readl_relaxed(iobase + ICMR);
	reg &= ~BIT(d->hwirq);
	writel_relaxed(reg, iobase + ICMR);
}

static void sa1100_unmask_irq(struct irq_data *d)
{
	u32 reg;

	reg = readl_relaxed(iobase + ICMR);
	reg |= BIT(d->hwirq);
	writel_relaxed(reg, iobase + ICMR);
}

static int sa1100_set_wake(struct irq_data *d, unsigned int on)
{
	return sa11x0_sc_set_wake(d->hwirq, on);
}

static struct irq_chip sa1100_normal_chip = {
	.name		= "SC",
	.irq_ack	= sa1100_mask_irq,
	.irq_mask	= sa1100_mask_irq,
	.irq_unmask	= sa1100_unmask_irq,
	.irq_set_wake	= sa1100_set_wake,
};

static int sa1100_normal_irqdomain_map(struct irq_domain *d,
		unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &sa1100_normal_chip,
				 handle_level_irq);

	return 0;
}

static const struct irq_domain_ops sa1100_normal_irqdomain_ops = {
	.map = sa1100_normal_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static struct irq_domain *sa1100_normal_irqdomain;

static struct sa1100irq_state {
	unsigned int	saved;
	unsigned int	icmr;
	unsigned int	iclr;
	unsigned int	iccr;
} sa1100irq_state;

static int sa1100irq_suspend(void)
{
	struct sa1100irq_state *st = &sa1100irq_state;

	st->saved = 1;
	st->icmr = readl_relaxed(iobase + ICMR);
	st->iclr = readl_relaxed(iobase + ICLR);
	st->iccr = readl_relaxed(iobase + ICCR);

	/*
	 * Disable all GPIO-based interrupts.
	 */
	writel_relaxed(st->icmr & 0xfffff000, iobase + ICMR);

	return 0;
}

static void sa1100irq_resume(void)
{
	struct sa1100irq_state *st = &sa1100irq_state;

	if (st->saved) {
		writel_relaxed(st->iccr, iobase + ICCR);
		writel_relaxed(st->iclr, iobase + ICLR);

		writel_relaxed(st->icmr, iobase + ICMR);
	}
}

static struct syscore_ops sa1100irq_syscore_ops = {
	.suspend	= sa1100irq_suspend,
	.resume		= sa1100irq_resume,
};

static int __init sa1100irq_init_devicefs(void)
{
	register_syscore_ops(&sa1100irq_syscore_ops);
	return 0;
}

device_initcall(sa1100irq_init_devicefs);

static void __exception_irq_entry sa1100_handle_irq(struct pt_regs *regs)
{
	uint32_t icip, icmr, mask;

	do {
		icip = readl_relaxed(iobase + ICIP);
		icmr = readl_relaxed(iobase + ICMR);
		mask = icip & icmr;

		if (mask == 0)
			break;

		generic_handle_domain_irq(sa1100_normal_irqdomain,
					  ffs(mask) - 1);
	} while (1);
}

void __init sa11x0_init_irq_nodt(int irq_start, resource_size_t io_start)
{
	iobase = ioremap(io_start, SZ_64K);
	if (WARN_ON(!iobase))
		return;

	/* disable all IRQs */
	writel_relaxed(0, iobase + ICMR);

	/* all IRQs are IRQ, not FIQ */
	writel_relaxed(0, iobase + ICLR);

	/*
	 * Whatever the doc says, this has to be set for the wait-on-irq
	 * instruction to work... on a SA1100 rev 9 at least.
	 */
	writel_relaxed(1, iobase + ICCR);

	sa1100_normal_irqdomain = irq_domain_add_simple(NULL,
			32, irq_start,
			&sa1100_normal_irqdomain_ops, NULL);

	set_handle_irq(sa1100_handle_irq);
}
