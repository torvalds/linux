/*
 * Copyright (C) 2017 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_DAIFFLAGS_H
#define __ASM_DAIFFLAGS_H

#include <linux/irqflags.h>

#define DAIF_PROCCTX		0
#define DAIF_PROCCTX_NOIRQ	PSR_I_BIT

/* mask/save/unmask/restore all exceptions, including interrupts. */
static inline void local_daif_mask(void)
{
	asm volatile(
		"msr	daifset, #0xf		// local_daif_mask\n"
		:
		:
		: "memory");
	trace_hardirqs_off();
}

static inline unsigned long local_daif_save(void)
{
	unsigned long flags;

	asm volatile(
		"mrs	%0, daif		// local_daif_save\n"
		: "=r" (flags)
		:
		: "memory");
	local_daif_mask();

	return flags;
}

static inline void local_daif_unmask(void)
{
	trace_hardirqs_on();
	asm volatile(
		"msr	daifclr, #0xf		// local_daif_unmask"
		:
		:
		: "memory");
}

static inline void local_daif_restore(unsigned long flags)
{
	if (!arch_irqs_disabled_flags(flags))
		trace_hardirqs_on();
	asm volatile(
		"msr	daif, %0		// local_daif_restore"
		:
		: "r" (flags)
		: "memory");
	if (arch_irqs_disabled_flags(flags))
		trace_hardirqs_off();
}

#endif
