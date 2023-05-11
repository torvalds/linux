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
 *   a2:	a0 of the caller in windowed ABI
 *   a10:	a0 of the caller in call0 ABI
 *
 * In call0 ABI the function _mcount is called with the special ABI:
 * its argument is in a10 and all the usual argument registers (a2 - a7)
 * must be preserved in addition to callee-saved a12 - a15.
 */

ENTRY(_mcount)
#if defined(__XTENSA_WINDOWED_ABI__)
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
#elif defined(__XTENSA_CALL0_ABI__)
	abi_entry_default

	movi	a9, ftrace_trace_function
	l32i	a9, a9, 0
	movi	a11, ftrace_stub
	bne	a9, a11, 1f
	abi_ret_default

1:	abi_entry(28)
	s32i	a0, sp, 0
	s32i	a2, sp, 4
	s32i	a3, sp, 8
	s32i	a4, sp, 12
	s32i	a5, sp, 16
	s32i	a6, sp, 20
	s32i	a7, sp, 24
	addi	a2, a10, -MCOUNT_INSN_SIZE
	callx0	a9
	l32i	a0, sp, 0
	l32i	a2, sp, 4
	l32i	a3, sp, 8
	l32i	a4, sp, 12
	l32i	a5, sp, 16
	l32i	a6, sp, 20
	l32i	a7, sp, 24
	abi_ret(28)
#else
#error Unsupported Xtensa ABI
#endif
ENDPROC(_mcount)
EXPORT_SYMBOL(_mcount)

ENTRY(ftrace_stub)
	abi_entry_default
	abi_ret_default
ENDPROC(ftrace_stub)
