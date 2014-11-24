/*
 * AArch64 KGDB support
 *
 * Based on arch/arm/include/kgdb.h
 *
 * Copyright (C) 2013 Cavium Inc.
 * Author: Vijaya Kumar K <vijaya.kumar@caviumnetworks.com>
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

#ifndef __ARM_KGDB_H
#define __ARM_KGDB_H

#include <linux/ptrace.h>
#include <asm/debug-monitors.h>

#ifndef	__ASSEMBLY__

static inline void arch_kgdb_breakpoint(void)
{
	asm ("brk %0" : : "I" (KGDB_COMPILED_DBG_BRK_IMM));
}

extern void kgdb_handle_bus_error(void);
extern int kgdb_fault_expected;

#endif /* !__ASSEMBLY__ */

/*
 * gdb is expecting the following registers layout.
 *
 * General purpose regs:
 *     r0-r30: 64 bit
 *     sp,pc : 64 bit
 *     pstate  : 64 bit
 *     Total: 34
 * FPU regs:
 *     f0-f31: 128 bit
 *     Total: 32
 * Extra regs
 *     fpsr & fpcr: 32 bit
 *     Total: 2
 *
 */

#define _GP_REGS		34
#define _FP_REGS		32
#define _EXTRA_REGS		2
/*
 * general purpose registers size in bytes.
 * pstate is only 4 bytes. subtract 4 bytes
 */
#define GP_REG_BYTES		(_GP_REGS * 8)
#define DBG_MAX_REG_NUM		(_GP_REGS + _FP_REGS + _EXTRA_REGS)

/*
 * Size of I/O buffer for gdb packet.
 * considering to hold all register contents, size is set
 */

#define BUFMAX			2048

/*
 * Number of bytes required for gdb_regs buffer.
 * _GP_REGS: 8 bytes, _FP_REGS: 16 bytes and _EXTRA_REGS: 4 bytes each
 * GDB fails to connect for size beyond this with error
 * "'g' packet reply is too long"
 */

#define NUMREGBYTES	((_GP_REGS * 8) + (_FP_REGS * 16) + \
			(_EXTRA_REGS * 4))

#endif /* __ASM_KGDB_H */
