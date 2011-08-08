/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/page.h>
#include <linux/io.h>
#include <linux/bug.h>

#include <asm/prom.h>
#include <asm/irq.h>

#ifdef CONFIG_SELFMOD_INTC
#include <asm/selfmod.h>
#define INTC_BASE	BARRIER_BASE_ADDR
#else
static unsigned int intc_baseaddr;
#define INTC_BASE	intc_baseaddr
#endif

unsigned int nr_irq;

/* No one else should require these constants, so define them locally here. */
#define ISR 0x00			/* Interrupt Status Register */
#define IPR 0x04			/* Interrupt Pending Register */
#define IER 0x08			/* Interrupt Enable Register */
#define IAR 0x0c			/* Interrupt Acknowledge Register */
#define SIE 0x10			/* Set Interrupt Enable bits */
#define CIE 0x14			/* Clear Interrupt Enable bits */
#define IVR 0x18			/* Interrupt Vector Register */
#define MER 0x1c			/* Master Enable Register */

#define MER_ME (1<<0)
#define MER_HIE (1<<1)

static void intc_enable_or_unmask(struct irq_data *d)
{
	unsigned long mask = 1 << d->irq;
	pr_debug("enable_or_unmask: %d\n", d->irq);
	out_be32(INTC_BASE + SIE, mask);

	/* ack level irqs because they can't be acked during
	 * ack function since the handle_level_irq function
	 * acks the irq before calling the interrupt handler
	 */
	if (irqd_is_level_type(d))
		out_be32(INTC_BASE + IAR, mask);
}

static void intc_disable_or_mask(struct irq_data *d)
{
	pr_debug("disable: %d\n", d->irq);
	out_be32(INTC_BASE + CIE, 1 << d->irq);
}

static void intc_ack(struct irq_data *d)
{
	pr_debug("ack: %d\n", d->irq);
	out_be32(INTC_BASE + IAR, 1 << d->irq);
}

static void intc_mask_ack(struct irq_data *d)
{
	unsigned long mask = 1 << d->irq;
	pr_debug("disable_and_ack: %d\n", d->irq);
	out_be32(INTC_BASE + CIE, mask);
	out_be32(INTC_BASE + IAR, mask);
}

static struct irq_chip intc_dev = {
	.name = "Xilinx INTC",
	.irq_unmask = intc_enable_or_unmask,
	.irq_mask = intc_disable_or_mask,
	.irq_ack = intc_ack,
	.irq_mask_ack = intc_mask_ack,
};

unsigned int get_irq(struct pt_regs *regs)
{
	int irq;

	/*
	 * NOTE: This function is the one that needs to be improved in
	 * order to handle multiple interrupt controllers. It currently
	 * is hardcoded to check for interrupts only on the first INTC.
	 */
	irq = in_be32(INTC_BASE + IVR);
	pr_debug("get_irq: %d\n", irq);

	return irq;
}

void __init init_IRQ(void)
{
	u32 i, j, intr_type;
	struct device_node *intc = NULL;
#ifdef CONFIG_SELFMOD_INTC
	unsigned int intc_baseaddr = 0;
	static int arr_func[] = {
				(int)&get_irq,
				(int)&intc_enable_or_unmask,
				(int)&intc_disable_or_mask,
				(int)&intc_mask_ack,
				(int)&intc_ack,
				(int)&intc_end,
				0
			};
#endif
	const char * const intc_list[] = {
				"xlnx,xps-intc-1.00.a",
				NULL
			};

	for (j = 0; intc_list[j] != NULL; j++) {
		intc = of_find_compatible_node(NULL, NULL, intc_list[j]);
		if (intc)
			break;
	}
	BUG_ON(!intc);

	intc_baseaddr = be32_to_cpup(of_get_property(intc,
								"reg", NULL));
	intc_baseaddr = (unsigned long) ioremap(intc_baseaddr, PAGE_SIZE);
	nr_irq = be32_to_cpup(of_get_property(intc,
						"xlnx,num-intr-inputs", NULL));

	intr_type =
		be32_to_cpup(of_get_property(intc,
						"xlnx,kind-of-intr", NULL));
	if (intr_type > (u32)((1ULL << nr_irq) - 1))
		printk(KERN_INFO " ERROR: Mismatch in kind-of-intr param\n");

#ifdef CONFIG_SELFMOD_INTC
	selfmod_function((int *) arr_func, intc_baseaddr);
#endif
	printk(KERN_INFO "%s #0 at 0x%08x, num_irq=%d, edge=0x%x\n",
		intc_list[j], intc_baseaddr, nr_irq, intr_type);

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	out_be32(intc_baseaddr + IER, 0);

	/* Acknowledge any pending interrupts just in case. */
	out_be32(intc_baseaddr + IAR, 0xffffffff);

	/* Turn on the Master Enable. */
	out_be32(intc_baseaddr + MER, MER_HIE | MER_ME);

	for (i = 0; i < nr_irq; ++i) {
		if (intr_type & (0x00000001 << i)) {
			irq_set_chip_and_handler_name(i, &intc_dev,
				handle_edge_irq, "edge");
			irq_clear_status_flags(i, IRQ_LEVEL);
		} else {
			irq_set_chip_and_handler_name(i, &intc_dev,
				handle_level_irq, "level");
			irq_set_status_flags(i, IRQ_LEVEL);
		}
	}
}
