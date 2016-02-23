/*
 * ARM KGDB support
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (C) 2002 MontaVista Software Inc.
 *
 */

#ifndef __ARM_KGDB_H__
#define __ARM_KGDB_H__

#include <linux/ptrace.h>
#include <asm/opcodes.h>

/*
 * GDB assumes that we're a user process being debugged, so
 * it will send us an SWI command to write into memory as the
 * debug trap. When an SWI occurs, the next instruction addr is
 * placed into R14_svc before jumping to the vector trap.
 * This doesn't work for kernel debugging as we are already in SVC
 * we would loose the kernel's LR, which is a bad thing. This
 * is  bad thing.
 *
 * By doing this as an undefined instruction trap, we force a mode
 * switch from SVC to UND mode, allowing us to save full kernel state.
 *
 * We also define a KGDB_COMPILED_BREAK which can be used to compile
 * in breakpoints. This is important for things like sysrq-G and for
 * the initial breakpoint from trap_init().
 *
 * Note to ARM HW designers: Add real trap support like SH && PPC to
 * make our lives much much simpler. :)
 */
#define BREAK_INSTR_SIZE	4
#define GDB_BREAKINST		0xef9f0001
#define KGDB_BREAKINST		0xe7ffdefe
#define KGDB_COMPILED_BREAK	0xe7ffdeff
#define CACHE_FLUSH_IS_SAFE	1

#ifndef	__ASSEMBLY__

static inline void arch_kgdb_breakpoint(void)
{
	asm(__inst_arm(0xe7ffdeff));
}

extern void kgdb_handle_bus_error(void);
extern int kgdb_fault_expected;

#endif /* !__ASSEMBLY__ */

/*
 * From Kevin Hilman:
 *
 * gdb is expecting the following registers layout.
 *
 * r0-r15: 1 long word each
 * f0-f7:  unused, 3 long words each !!
 * fps:    unused, 1 long word
 * cpsr:   1 long word
 *
 * Even though f0-f7 and fps are not used, they need to be
 * present in the registers sent for correct processing in
 * the host-side gdb.
 *
 * In particular, it is crucial that CPSR is in the right place,
 * otherwise gdb will not be able to correctly interpret stepping over
 * conditional branches.
 */
#define _GP_REGS		16
#define _FP_REGS		8
#define _EXTRA_REGS		2
#define GDB_MAX_REGS		(_GP_REGS + (_FP_REGS * 3) + _EXTRA_REGS)
#define DBG_MAX_REG_NUM		(_GP_REGS + _FP_REGS + _EXTRA_REGS)

#define KGDB_MAX_NO_CPUS	1
#define BUFMAX			400
#define NUMREGBYTES		(DBG_MAX_REG_NUM << 2)
#define NUMCRITREGBYTES		(32 << 2)

#define _R0			0
#define _R1			1
#define _R2			2
#define _R3			3
#define _R4			4
#define _R5			5
#define _R6			6
#define _R7			7
#define _R8			8
#define _R9			9
#define _R10			10
#define _FP			11
#define _IP			12
#define _SPT			13
#define _LR			14
#define _PC			15
#define _CPSR			(GDB_MAX_REGS - 1)

/*
 * So that we can denote the end of a frame for tracing,
 * in the simple case:
 */
#define CFI_END_FRAME(func)	__CFI_END_FRAME(_PC, _SPT, func)

#endif /* __ASM_KGDB_H__ */
