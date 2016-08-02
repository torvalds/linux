/*
 * Copyright (C) BitBox Ltd 2010
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
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
