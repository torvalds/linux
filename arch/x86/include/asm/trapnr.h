/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPNR_H
#define _ASM_X86_TRAPNR_H

/*
 * Event type codes used by FRED, Intel VT-x and AMD SVM
 */
#define EVENT_TYPE_EXTINT	0	// External interrupt
#define EVENT_TYPE_RESERVED	1
#define EVENT_TYPE_NMI		2	// NMI
#define EVENT_TYPE_HWEXC	3	// Hardware originated traps, exceptions
#define EVENT_TYPE_SWINT	4	// INT n
#define EVENT_TYPE_PRIV_SWEXC	5	// INT1
#define EVENT_TYPE_SWEXC	6	// INTO, INT3
#define EVENT_TYPE_OTHER	7	// FRED SYSCALL/SYSENTER, VT-x MTF

/* Interrupts/Exceptions */

#define X86_TRAP_DE		 0	/* Divide-by-zero */
#define X86_TRAP_DB		 1	/* Debug */
#define X86_TRAP_NMI		 2	/* Non-maskable Interrupt */
#define X86_TRAP_BP		 3	/* Breakpoint */
#define X86_TRAP_OF		 4	/* Overflow */
#define X86_TRAP_BR		 5	/* Bound Range Exceeded */
#define X86_TRAP_UD		 6	/* Invalid Opcode */
#define X86_TRAP_NM		 7	/* Device Not Available */
#define X86_TRAP_DF		 8	/* Double Fault */
#define X86_TRAP_OLD_MF		 9	/* Coprocessor Segment Overrun */
#define X86_TRAP_TS		10	/* Invalid TSS */
#define X86_TRAP_NP		11	/* Segment Not Present */
#define X86_TRAP_SS		12	/* Stack Segment Fault */
#define X86_TRAP_GP		13	/* General Protection Fault */
#define X86_TRAP_PF		14	/* Page Fault */
#define X86_TRAP_SPURIOUS	15	/* Spurious Interrupt */
#define X86_TRAP_MF		16	/* x87 Floating-Point Exception */
#define X86_TRAP_AC		17	/* Alignment Check */
#define X86_TRAP_MC		18	/* Machine Check */
#define X86_TRAP_XF		19	/* SIMD Floating-Point Exception */
#define X86_TRAP_VE		20	/* Virtualization Exception */
#define X86_TRAP_CP		21	/* Control Protection Exception */
#define X86_TRAP_VC		29	/* VMM Communication Exception */
#define X86_TRAP_IRET		32	/* IRET Exception */

#endif
