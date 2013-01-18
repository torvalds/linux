/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/irqflags.h>
#include <asm/arcregs.h>

void arch_local_irq_enable(void)
{
	unsigned long flags;

	/*
	 * ARC IDE Drivers tries to re-enable interrupts from hard-isr
	 * context which is simply wrong
	 */
	if (in_irq()) {
		WARN_ONCE(1, "IRQ enabled from hard-isr");
		return;
	}

	flags = arch_local_save_flags();
	flags |= (STATUS_E1_MASK | STATUS_E2_MASK);
	arch_local_irq_restore(flags);
}
EXPORT_SYMBOL(arch_local_irq_enable);
