/*
 * arch/xtensa/kernel/mcount.S
 *
 * Xtensa specific mcount support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Tensilica Inc.
 */

#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/ftrace.h>

/*
 * Entry condition:
 *
 *   a2:	a0 of the caller
 */

ENTRY(_mcount)

	abi_entry_default

	movi	a4, ftrace_trace_function
	l32i	a4, a4, 0
	movi	a3, ftrace_stub
	bne	a3, a4, 1f
	abi_ret_default

1: 	xor	a7, a2, a1
	movi	a3, 0x3fffffff
	and	a7, a7, a3
	xor	a7, a7, a1

	xor	a6, a0, a1
	and	a6, a6, a3
	xor	a6, a6, a1
	addi	a6, a6, -MCOUNT_INSN_SIZE
	callx4	a4

	abi_ret_default

ENDPROC(_mcount)

ENTRY(ftrace_stub)
	abi_entry_default
	abi_ret_default
ENDPROC(ftrace_stub)
