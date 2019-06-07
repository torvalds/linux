// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/spinlock.h>
#include <linux/export.h>

#include <asm/barrier.h>
#include <asm/krait-l2-accessors.h>

static DEFINE_RAW_SPINLOCK(krait_l2_lock);

void krait_set_l2_indirect_reg(u32 addr, u32 val)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&krait_l2_lock, flags);
	/*
	 * Select the L2 window by poking l2cpselr, then write to the window
	 * via l2cpdr.
	 */
	asm volatile ("mcr p15, 3, %0, c15, c0, 6 @ l2cpselr" : : "r" (addr));
	isb();
	asm volatile ("mcr p15, 3, %0, c15, c0, 7 @ l2cpdr" : : "r" (val));
	isb();

	raw_spin_unlock_irqrestore(&krait_l2_lock, flags);
}
EXPORT_SYMBOL(krait_set_l2_indirect_reg);

u32 krait_get_l2_indirect_reg(u32 addr)
{
	u32 val;
	unsigned long flags;

	raw_spin_lock_irqsave(&krait_l2_lock, flags);
	/*
	 * Select the L2 window by poking l2cpselr, then read from the window
	 * via l2cpdr.
	 */
	asm volatile ("mcr p15, 3, %0, c15, c0, 6 @ l2cpselr" : : "r" (addr));
	isb();
	asm volatile ("mrc p15, 3, %0, c15, c0, 7 @ l2cpdr" : "=r" (val));

	raw_spin_unlock_irqrestore(&krait_l2_lock, flags);

	return val;
}
EXPORT_SYMBOL(krait_get_l2_indirect_reg);
