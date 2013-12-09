/*
 * wrapper/irqdesc.c
 *
 * wrapper around irq_to_desc. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef CONFIG_KALLSYMS

#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include "kallsyms.h"
#include "irqdesc.h"

static
struct irq_desc *(*irq_to_desc_sym)(unsigned int irq);

struct irq_desc *wrapper_irq_to_desc(unsigned int irq)
{
	if (!irq_to_desc_sym)
		irq_to_desc_sym = (void *) kallsyms_lookup_funcptr("irq_to_desc");
	if (irq_to_desc_sym) {
		return irq_to_desc_sym(irq);
	} else {
		printk(KERN_WARNING "LTTng: irq_to_desc symbol lookup failed.\n");
		return NULL;
	}
}

#else

#include <linux/interrupt.h>
#include <linux/irqnr.h>

struct irq_desc *wrapper_irq_to_desc(unsigned int irq)
{
	return irq_to_desc(irq);
}

#endif
