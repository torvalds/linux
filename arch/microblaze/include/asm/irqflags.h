/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_IRQFLAGS_H
#define _ASM_MICROBLAZE_IRQFLAGS_H

#include <linux/irqflags.h>
#include <asm/registers.h>

# if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR

# define raw_local_irq_save(flags)			\
	do {						\
		asm volatile ("	msrclr %0, %1;		\
				nop;"			\
				: "=r"(flags)		\
				: "i"(MSR_IE)		\
				: "memory");		\
	} while (0)

# define raw_local_irq_disable()			\
	do {						\
		asm volatile ("	msrclr r0, %0;		\
				nop;"			\
				:			\
				: "i"(MSR_IE)		\
				: "memory");		\
	} while (0)

# define raw_local_irq_enable()				\
	do {						\
		asm volatile ("	msrset	r0, %0;		\
				nop;"			\
				:			\
				: "i"(MSR_IE)		\
				: "memory");		\
	} while (0)

# else /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR == 0 */

# define raw_local_irq_save(flags)				\
	do {							\
		register unsigned tmp;				\
		asm volatile ("	mfs	%0, rmsr;		\
				nop;				\
				andi	%1, %0, %2;		\
				mts	rmsr, %1;		\
				nop;"				\
				: "=r"(flags), "=r" (tmp)	\
				: "i"(~MSR_IE)			\
				: "memory");			\
	} while (0)

# define raw_local_irq_disable()				\
	do {							\
		register unsigned tmp;				\
		asm volatile ("	mfs	%0, rmsr;		\
				nop;				\
				andi	%0, %0, %1;		\
				mts	rmsr, %0;		\
				nop;"			\
				: "=r"(tmp)			\
				: "i"(~MSR_IE)			\
				: "memory");			\
	} while (0)

# define raw_local_irq_enable()					\
	do {							\
		register unsigned tmp;				\
		asm volatile ("	mfs	%0, rmsr;		\
				nop;				\
				ori	%0, %0, %1;		\
				mts	rmsr, %0;		\
				nop;"				\
				: "=r"(tmp)			\
				: "i"(MSR_IE)			\
				: "memory");			\
	} while (0)

# endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

#define raw_local_irq_restore(flags)				\
	do {							\
		asm volatile ("	mts	rmsr, %0;		\
				nop;"				\
				:				\
				: "r"(flags)			\
				: "memory");			\
	} while (0)

static inline unsigned long get_msr(void)
{
	unsigned long flags;
	asm volatile ("	mfs	%0, rmsr;	\
			nop;"			\
			: "=r"(flags)		\
			:			\
			: "memory");		\
	return flags;
}

#define raw_local_save_flags(flags)	((flags) = get_msr())
#define raw_irqs_disabled()		((get_msr() & MSR_IE) == 0)
#define raw_irqs_disabled_flags(flags)	((flags & MSR_IE) == 0)

#endif /* _ASM_MICROBLAZE_IRQFLAGS_H */
