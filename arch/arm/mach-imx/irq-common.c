// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) BitBox Ltd 2010
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/platform_data/asoc-imx-ssi.h>

#include "irq-common.h"

int mxc_set_irq_fiq(unsigned int irq, unsigned int type)
{
	struct irq_chip_generic *gc;
	struct mxc_extra_irq *exirq;
	int ret;

	ret = -ENOSYS;

	gc = irq_get_chip_data(irq);
	if (gc && gc->private) {
		exirq = gc->private;
		if (exirq->set_irq_fiq) {
			struct irq_data *d = irq_get_irq_data(irq);
			ret = exirq->set_irq_fiq(irqd_to_hwirq(d), type);
		}
	}

	return ret;
}
EXPORT_SYMBOL(mxc_set_irq_fiq);
