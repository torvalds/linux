/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IRQ support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/hexagon_vm.h>
#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	return __vmgetie();
}

static inline unsigned long arch_local_irq_save(void)
{
	return __vmsetie(VM_INT_DISABLE);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return !flags;
}

static inline bool arch_irqs_disabled(void)
{
	return !__vmgetie();
}

static inline void arch_local_irq_enable(void)
{
	__vmsetie(VM_INT_ENABLE);
}

static inline void arch_local_irq_disable(void)
{
	__vmsetie(VM_INT_DISABLE);
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	__vmsetie(flags);
}

#endif
