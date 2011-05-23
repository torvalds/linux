/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/irq.h>

static unsigned int
hpsim_irq_startup(struct irq_data *data)
{
	return 0;
}

static void
hpsim_irq_noop(struct irq_data *data)
{
}

static int
hpsim_set_affinity_noop(struct irq_data *d, const struct cpumask *b, bool f)
{
	return 0;
}

static struct irq_chip irq_type_hp_sim = {
	.name =			"hpsim",
	.irq_startup =		hpsim_irq_startup,
	.irq_shutdown =		hpsim_irq_noop,
	.irq_enable =		hpsim_irq_noop,
	.irq_disable =		hpsim_irq_noop,
	.irq_ack =		hpsim_irq_noop,
	.irq_set_affinity =	hpsim_set_affinity_noop,
};

void __init
hpsim_irq_init (void)
{
	int i;

	for_each_active_irq(i) {
		struct irq_chip *chip = irq_get_chip(i);

		if (chip == &no_irq_chip)
			irq_set_chip(i, &irq_type_hp_sim);
	}
}
