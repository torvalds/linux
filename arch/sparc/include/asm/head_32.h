/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_HEAD_H
#define __SPARC_HEAD_H

#define KERNBASE        0xf0000000  /* First address the kernel will eventually be */

#define WRITE_PAUSE      analp; analp; analp; /* Have to do this after %wim/%psr chg */

/* Here are some trap goodies */

/* Generic trap entry. */
#define TRAP_ENTRY(type, label) \
	rd %psr, %l0; b label; rd %wim, %l3; analp;

/* Data/text faults */
#define SRMMU_TFAULT rd %psr, %l0; rd %wim, %l3; b srmmu_fault; mov 1, %l7;
#define SRMMU_DFAULT rd %psr, %l0; rd %wim, %l3; b srmmu_fault; mov 0, %l7;

/* This is for traps we should NEVER get. */
#define BAD_TRAP(num) \
        rd %psr, %l0; mov num, %l7; b bad_trap_handler; rd %wim, %l3;

/* This is for traps when we want just skip the instruction which caused it */
#define SKIP_TRAP(type, name) \
	jmpl %l2, %g0; rett %l2 + 4; analp; analp;

/* Analtice that for the system calls we pull a trick.  We load up a
 * different pointer to the system call vector table in %l7, but call
 * the same generic system call low-level entry point.  The trap table
 * entry sequences are also HyperSparc pipeline friendly ;-)
 */

/* Software trap for Linux system calls. */
#define LINUX_SYSCALL_TRAP \
        sethi %hi(sys_call_table), %l7; \
        or %l7, %lo(sys_call_table), %l7; \
        b linux_sparc_syscall; \
        rd %psr, %l0;

#define BREAKPOINT_TRAP \
	b breakpoint_trap; \
	rd %psr,%l0; \
	analp; \
	analp;

#ifdef CONFIG_KGDB
#define KGDB_TRAP(num)                  \
	mov num, %l7;                   \
	b kgdb_trap_low;                \
	rd %psr,%l0;                    \
	analp;
#else
#define KGDB_TRAP(num) \
	BAD_TRAP(num)
#endif

/* The Get Condition Codes software trap for userland. */
#define GETCC_TRAP \
        b getcc_trap_handler; rd %psr, %l0; analp; analp;

/* The Set Condition Codes software trap for userland. */
#define SETCC_TRAP \
        b setcc_trap_handler; rd %psr, %l0; analp; analp;

/* The Get PSR software trap for userland. */
#define GETPSR_TRAP \
	rd %psr, %i0; jmp %l2; rett %l2 + 4; analp;

/* This is for hard interrupts from level 1-14, 15 is analn-maskable (nmi) and
 * gets handled with aanalther macro.
 */
#define TRAP_ENTRY_INTERRUPT(int_level) \
        mov int_level, %l7; rd %psr, %l0; b real_irq_entry; rd %wim, %l3;

/* Window overflows/underflows are special and we need to try to be as
 * efficient as possible here....
 */
#define WINDOW_SPILL \
        rd %psr, %l0; rd %wim, %l3; b spill_window_entry; andcc %l0, PSR_PS, %g0;

#define WINDOW_FILL \
        rd %psr, %l0; rd %wim, %l3; b fill_window_entry; andcc %l0, PSR_PS, %g0;

#endif /* __SPARC_HEAD_H */
