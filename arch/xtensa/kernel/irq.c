/*
 * linux/arch/xtensa/kernel/irq.c
 *
 * Xtensa built-in interrupt controller and some generic functions copied
 * from i386.
 *
 * Copyright (C) 2002 - 2006 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 *
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

#include <asm/uaccess.h>
#include <asm/platform.h>

static unsigned int cached_irq_mask;

atomic_t irq_err_count;

static struct irq_domain *root_domain;

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

asmlinkage void do_IRQ(int hwirq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	int irq = irq_find_mapping(root_domain, hwirq);

	if (hwirq >= NR_IRQS) {
		printk(KERN_EMERG "%s: cannot handle IRQ %d\n",
				__func__, hwirq);
	}

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		unsigned long sp;

		__asm__ __volatile__ ("mov %0, a1\n" : "=a" (sp));
		sp &= THREAD_SIZE - 1;

		if (unlikely(sp < (sizeof(thread_info) + 1024)))
			printk("Stack overflow in do_IRQ: %ld\n",
			       sp - sizeof(struct thread_info));
	}
#endif
	generic_handle_irq(irq);

	irq_exit();
	set_irq_regs(old_regs);
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	seq_printf(p, "%*s: ", prec, "ERR");
	seq_printf(p, "%10u\n", atomic_read(&irq_err_count));
	return 0;
}

static void xtensa_irq_mask(struct irq_data *d)
{
	cached_irq_mask &= ~(1 << d->hwirq);
	set_sr (cached_irq_mask, intenable);
}

static void xtensa_irq_unmask(struct irq_data *d)
{
	cached_irq_mask |= 1 << d->hwirq;
	set_sr (cached_irq_mask, intenable);
}

static void xtensa_irq_enable(struct irq_data *d)
{
	variant_irq_enable(d->hwirq);
	xtensa_irq_unmask(d);
}

static void xtensa_irq_disable(struct irq_data *d)
{
	xtensa_irq_mask(d);
	variant_irq_disable(d->hwirq);
}

static void xtensa_irq_ack(struct irq_data *d)
{
	set_sr(1 << d->hwirq, intclear);
}

static int xtensa_irq_retrigger(struct irq_data *d)
{
	set_sr(1 << d->hwirq, intset);
	return 1;
}

static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.irq_enable	= xtensa_irq_enable,
	.irq_disable	= xtensa_irq_disable,
	.irq_mask	= xtensa_irq_mask,
	.irq_unmask	= xtensa_irq_unmask,
	.irq_ack	= xtensa_irq_ack,
	.irq_retrigger	= xtensa_irq_retrigger,
};

static int xtensa_irq_map(struct irq_domain *d, unsigned int irq,
		irq_hw_number_t hw)
{
	u32 mask = 1 << hw;

	if (mask & XCHAL_INTTYPE_MASK_SOFTWARE) {
		irq_set_chip_and_handler_name(irq, &xtensa_irq_chip,
				handle_simple_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_EXTERN_EDGE) {
		irq_set_chip_and_handler_name(irq, &xtensa_irq_chip,
				handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_EXTERN_LEVEL) {
		irq_set_chip_and_handler_name(irq, &xtensa_irq_chip,
				handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_TIMER) {
		irq_set_chip_and_handler_name(irq, &xtensa_irq_chip,
				handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else {/* XCHAL_INTTYPE_MASK_WRITE_ERROR */
		/* XCHAL_INTTYPE_MASK_NMI */

		irq_set_chip_and_handler_name(irq, &xtensa_irq_chip,
				handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	}
	return 0;
}

static unsigned map_ext_irq(unsigned ext_irq)
{
	unsigned mask = XCHAL_INTTYPE_MASK_EXTERN_EDGE |
		XCHAL_INTTYPE_MASK_EXTERN_LEVEL;
	unsigned i;

	for (i = 0; mask; ++i, mask >>= 1) {
		if ((mask & 1) && ext_irq-- == 0)
			return i;
	}
	return XCHAL_NUM_INTERRUPTS;
}

/*
 * Device Tree IRQ specifier translation function which works with one or
 * two cell bindings. First cell value maps directly to the hwirq number.
 * Second cell if present specifies whether hwirq number is external (1) or
 * internal (0).
 */
int xtensa_irq_domain_xlate(struct irq_domain *d, struct device_node *ctrlr,
		const u32 *intspec, unsigned int intsize,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1 || intsize > 2))
		return -EINVAL;
	if (intsize == 2 && intspec[1] == 1) {
		unsigned int_irq = map_ext_irq(intspec[0]);
		if (int_irq < XCHAL_NUM_INTERRUPTS)
			*out_hwirq = int_irq;
		else
			return -EINVAL;
	} else {
		*out_hwirq = intspec[0];
	}
	*out_type = IRQ_TYPE_NONE;
	return 0;
}

static const struct irq_domain_ops xtensa_irq_domain_ops = {
	.xlate = xtensa_irq_domain_xlate,
	.map = xtensa_irq_map,
};

void __init init_IRQ(void)
{
	struct device_node *intc = NULL;

	cached_irq_mask = 0;
	set_sr(~0, intclear);

#ifdef CONFIG_OF
	/* The interrupt controller device node is mandatory */
	intc = of_find_compatible_node(NULL, NULL, "xtensa,pic");
	BUG_ON(!intc);

	root_domain = irq_domain_add_linear(intc, NR_IRQS,
			&xtensa_irq_domain_ops, NULL);
#else
	root_domain = irq_domain_add_legacy(intc, NR_IRQS, 0, 0,
			&xtensa_irq_domain_ops, NULL);
#endif
	irq_set_default_host(root_domain);

	variant_init_irq();
}
