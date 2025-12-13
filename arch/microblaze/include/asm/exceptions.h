/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Preliminary support for HW exception handing for Microblaze
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 */

#ifndef _ASM_MICROBLAZE_EXCEPTIONS_H
#define _ASM_MICROBLAZE_EXCEPTIONS_H

#ifdef __KERNEL__
#ifndef __ASSEMBLER__

/* Macros to enable and disable HW exceptions in the MSR */
/* Define MSR enable bit for HW exceptions */
#define HWEX_MSR_BIT (1 << 8)

#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
#define __enable_hw_exceptions()					\
	__asm__ __volatile__ ("	msrset	r0, %0;				\
				nop;"					\
				:					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory")

#define __disable_hw_exceptions()					\
	__asm__ __volatile__ ("	msrclr r0, %0;				\
				nop;"					\
				:					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory")
#else /* !CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */
#define __enable_hw_exceptions()					\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				nop;					\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory", "r12")

#define __disable_hw_exceptions()					\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				nop;					\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory", "r12")
#endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

asmlinkage void full_exception(struct pt_regs *regs, unsigned int type,
							int fsr, int addr);

asmlinkage void sw_exception(struct pt_regs *regs);
void bad_page_fault(struct pt_regs *regs, unsigned long address, int sig);

void die(const char *str, struct pt_regs *fp, long err);
void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr);

#endif /*__ASSEMBLER__ */
#endif /* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_EXCEPTIONS_H */
