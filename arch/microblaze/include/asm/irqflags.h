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

# if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR

# define local_irq_save(flags)				\
	do {						\
		asm volatile ("# local_irq_save	\n\t"	\
				"msrclr %0, %1	\n\t"	\
				"nop	\n\t"		\
				: "=r"(flags)		\
				: "i"(MSR_IE)		\
				: "memory");		\
	} while (0)

# define local_irq_disable()					\
	do {							\
		asm volatile ("# local_irq_disable \n\t"	\
				"msrclr r0, %0 \n\t"		\
				"nop	\n\t"			\
				:				\
				: "i"(MSR_IE)			\
				: "memory");			\
	} while (0)

# define local_irq_enable()					\
	do {							\
		asm volatile ("# local_irq_enable \n\t"		\
				"msrset	r0, %0 \n\t"		\
				"nop	\n\t"			\
				:				\
				: "i"(MSR_IE)			\
				: "memory");			\
	} while (0)

# else /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR == 0 */

# define local_irq_save(flags)					\
	do {							\
		register unsigned tmp;				\
		asm volatile ("# local_irq_save	\n\t"		\
				"mfs	%0, rmsr \n\t"		\
				"nop \n\t"			\
				"andi	%1, %0, %2 \n\t"	\
				"mts	rmsr, %1 \n\t"		\
				"nop \n\t"			\
				: "=r"(flags), "=r" (tmp)	\
				: "i"(~MSR_IE)			\
				: "memory");			\
	} while (0)

# define local_irq_disable()					\
	do {							\
		register unsigned tmp;				\
		asm volatile ("# local_irq_disable \n\t"	\
				"mfs	%0, rmsr \n\t"		\
				"nop \n\t"			\
				"andi	%0, %0, %1 \n\t"	\
				"mts	rmsr, %0 \n\t"		\
				"nop \n\t"			\
				: "=r"(tmp)			\
				: "i"(~MSR_IE)			\
				: "memory");			\
	} while (0)

# define local_irq_enable()					\
	do {							\
		register unsigned tmp;				\
		asm volatile ("# local_irq_enable \n\t"		\
				"mfs	%0, rmsr \n\t"		\
				"nop \n\t"			\
				"ori	%0, %0, %1 \n\t"	\
				"mts	rmsr, %0 \n\t"		\
				"nop \n\t"			\
				: "=r"(tmp)			\
				: "i"(MSR_IE)			\
				: "memory");			\
	} while (0)

# endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

#define local_save_flags(flags)					\
	do {							\
		asm volatile ("# local_save_flags \n\t"		\
				"mfs	%0, rmsr \n\t"		\
				"nop	\n\t"			\
				: "=r"(flags)			\
				:				\
				: "memory");			\
	} while (0)

#define local_irq_restore(flags)			\
	do {						\
		asm volatile ("# local_irq_restore \n\t"\
				"mts	rmsr, %0 \n\t"	\
				"nop	\n\t"		\
				:			\
				: "r"(flags)		\
				: "memory");		\
	} while (0)

static inline int irqs_disabled(void)
{
	unsigned long flags;

	local_save_flags(flags);
	return ((flags & MSR_IE) == 0);
}

#define raw_irqs_disabled irqs_disabled
#define raw_irqs_disabled_flags(flags)	((flags) == 0)

#endif /* _ASM_MICROBLAZE_IRQFLAGS_H */
