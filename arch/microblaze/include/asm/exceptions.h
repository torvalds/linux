/*
 * Preliminary support for HW exception handing for Microblaze
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_EXCEPTIONS_H
#define _ASM_MICROBLAZE_EXCEPTIONS_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

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

void die(const char *str, struct pt_regs *fp, long err);
void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr);

#ifdef CONFIG_MMU
void __bug(const char *file, int line, void *data);
int bad_trap(int trap_num, struct pt_regs *regs);
int debug_trap(struct pt_regs *regs);
#endif /* CONFIG_MMU */

#if defined(CONFIG_KGDB)
void (*debugger)(struct pt_regs *regs);
int (*debugger_bpt)(struct pt_regs *regs);
int (*debugger_sstep)(struct pt_regs *regs);
int (*debugger_iabr_match)(struct pt_regs *regs);
int (*debugger_dabr_match)(struct pt_regs *regs);
void (*debugger_fault_handler)(struct pt_regs *regs);
#else
#define debugger(regs)			do { } while (0)
#define debugger_bpt(regs)		0
#define debugger_sstep(regs)		0
#define debugger_iabr_match(regs)	0
#define debugger_dabr_match(regs)	0
#define debugger_fault_handler		((void (*)(struct pt_regs *))0)
#endif

#endif /*__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_EXCEPTIONS_H */
