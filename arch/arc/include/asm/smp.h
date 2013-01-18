/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_SMP_H
#define __ASM_ARC_SMP_H

/*
 * ARC700 doesn't support atomic Read-Modify-Write ops.
 * Originally Interrupts had to be disabled around code to gaurantee atomicity.
 * The LLOCK/SCOND insns allow writing interrupt-hassle-free based atomic ops
 * based on retry-if-irq-in-atomic (with hardware assist).
 * However despite these, we provide the IRQ disabling variant
 *
 * (1) These insn were introduced only in 4.10 release. So for older released
 *	support needed.
 */
#ifndef CONFIG_ARC_HAS_LLSC

#include <linux/irqflags.h>

#define atomic_ops_lock(flags)		local_irq_save(flags)
#define atomic_ops_unlock(flags)	local_irq_restore(flags)

#define bitops_lock(flags)		local_irq_save(flags)
#define bitops_unlock(flags)		local_irq_restore(flags)

#endif	/* !CONFIG_ARC_HAS_LLSC */

#endif
