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
 * gdb remote procotol (well most versions of it) expects the following
 * register layout.
 *
 * General purpose regs:
 *     r0-r30: 64 bit
 *     sp,pc : 64 bit
 *     pstate  : 32 bit
 *     Total: 33 + 1
 * FPU regs:
 *     f0-f31: 128 bit
 *     fpsr & fpcr: 32 bit
 *     Total: 32 + 2
 *
 * To expand a little on the "most versions of it"... when the gdb remote
 * protocol for AArch64 was developed it depended on a statement in the
 * Architecture Reference Manual that claimed "SPSR_ELx is a 32-bit register".
 * and, as a result, allocated only 32-bits for the PSTATE in the remote
 * protocol. In fact this statement is still present in ARM DDI 0487A.i.
 *
 * Unfortunately "is a 32-bit register" has a very special meaning for
 * system registers. It means that "the upper bits, bits[63:32], are
 * RES0.". RES0 is heavily used in the ARM architecture documents as a
 * way to leave space for future architecture changes. So to translate a
 * little for people who don't spend their spare time reading ARM architecture
 * manuals, what "is a 32-bit register" actually means in this context is
 * "is a 64-bit register but one with no meaning allocated to any of the
 * upper 32-bits... *yet*".
 *
 * Perhaps then we should not be surprised that this has led to some
 * confusion. Specifically a patch, influenced by the above translation,
 * that extended PSTATE to 64-bit was accepted into gdb-7.7 but the patch
 * was reverted in gdb-7.8.1 and all later releases, when this was
 * discovered to be an undocumented protocol change.
 *
 * So... it is *not* wrong for us to only allocate 32-bits to PSTATE
 * here even though the kernel itself allocates 64-bits for the same
 * state. That is because this bit of code tells the kernel how the gdb
 * remote protocol (well most versions of it) describes the register state.
 *
 * Note that if you are using one of the versions of gdb that supports
 * the gdb-7.7 version of the protocol you cannot use kgdb directly
 * without providing a custom register description (gdb can load new
 * protocol descriptions at runtime).
 */

#define _GP_REGS		33
#define _FP_REGS		32
#define _EXTRA_REGS		3
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
