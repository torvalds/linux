/*
 * linux/arch/sh/boards/se/7751/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 * Modified for 7751 Solution Engine by
 * Ian da Silva and Jeremy Siegel, 2001.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <mach-se/mach/se7751.h>

static struct ipr_data ipr_irq_table[] = {
	{ 13, 3, 3, 2 },
	/* Add additional entries here as drivers are added and tested. */
};

static unsigned long ipr_offsets[] = {
	BCR_ILCRA,
	BCR_ILCRB,
	BCR_ILCRC,
	BCR_ILCRD,
	BCR_ILCRE,
	BCR_ILCRF,
	BCR_ILCRG,
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-se7751",
	},
};

/*
 * Initialize IRQ setting
 */
void __init init_7751se_IRQ(void)
{
	register_ipr_controller(&ipr_irq_desc);
}
