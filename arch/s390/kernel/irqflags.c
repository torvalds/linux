// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <asm/irqflags.h>

noinstr unsigned long arch_local_save_flags(void)
{
	return __arch_local_save_flags();
}
EXPORT_SYMBOL(arch_local_save_flags);

noinstr unsigned long arch_local_irq_save(void)
{
	return __arch_local_irq_save();
}
EXPORT_SYMBOL(arch_local_irq_save);

noinstr void arch_local_irq_enable_external(void)
{
	__arch_local_irq_enable_external();
}
EXPORT_SYMBOL(arch_local_irq_enable_external);

noinstr void arch_local_irq_enable(void)
{
	__arch_local_irq_enable();
}
EXPORT_SYMBOL(arch_local_irq_enable);
