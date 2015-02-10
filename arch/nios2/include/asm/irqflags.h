/*
 * Copyright (C) 2010 Thomas Chou <thomas@wytron.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/registers.h>

static inline unsigned long arch_local_save_flags(void)
{
	return RDCTL(CTL_STATUS);
}

/*
 * This will restore ALL status register flags, not only the interrupt
 * mask flag.
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	WRCTL(CTL_STATUS, flags);
}

static inline void arch_local_irq_disable(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags & ~STATUS_PIE);
}

static inline void arch_local_irq_enable(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags | STATUS_PIE);
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & STATUS_PIE) == 0;
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags & ~STATUS_PIE);
	return flags;
}

#endif /* _ASM_IRQFLAGS_H */
