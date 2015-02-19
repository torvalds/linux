/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE-Gx KGDB support.
 */

#ifndef __TILE_KGDB_H__
#define __TILE_KGDB_H__

#include <linux/kdebug.h>
#include <arch/opcode.h>

#define GDB_SIZEOF_REG		sizeof(unsigned long)

/*
 * TILE-Gx gdb is expecting the following register layout:
 * 56 GPRs(R0 - R52, TP, SP, LR), 8 special GPRs(networks and ZERO),
 * plus the PC and the faultnum.
 *
 * Even though kernel not use the 8 special GPRs, they need to be present
 * in the registers sent for correct processing in the host-side gdb.
 *
 */
#define DBG_MAX_REG_NUM		(56+8+2)
#define NUMREGBYTES		(DBG_MAX_REG_NUM * GDB_SIZEOF_REG)

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound
 * buffers at least NUMREGBYTES*2 are needed for register packets,
 * Longer buffer is needed to list all threads.
 */
#define BUFMAX			2048

#define BREAK_INSTR_SIZE	TILEGX_BUNDLE_SIZE_IN_BYTES

/*
 * Require cache flush for set/clear a software breakpoint or write memory.
 */
#define CACHE_FLUSH_IS_SAFE	1

/*
 * The compiled-in breakpoint instruction can be used to "break" into
 * the debugger via magic system request key (sysrq-G).
 */
static tile_bundle_bits compiled_bpt = TILEGX_BPT_BUNDLE | DIE_COMPILED_BPT;

enum tilegx_regnum {
	TILEGX_PC_REGNUM = TREG_LAST_GPR + 9,
	TILEGX_FAULTNUM_REGNUM,
};

/*
 * Generate a breakpoint exception to "break" into the debugger.
 */
static inline void arch_kgdb_breakpoint(void)
{
	asm volatile (".quad %0\n\t"
		      ::""(compiled_bpt));
}

#endif /* __TILE_KGDB_H__ */
