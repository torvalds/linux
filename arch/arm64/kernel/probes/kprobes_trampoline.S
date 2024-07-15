/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trampoline entry and return code for kretprobes.
 */

#include <linux/linkage.h>
#include <asm/asm-bug.h>
#include <asm/assembler.h>

	.text

SYM_CODE_START(__kretprobe_trampoline)
	/*
	 * Trigger a breakpoint exception. The PC will be adjusted by
	 * kretprobe_breakpoint_handler(), and no subsequent instructions will
	 * be executed from the trampoline.
	 */
	brk #KRETPROBES_BRK_IMM
	ASM_BUG()
SYM_CODE_END(__kretprobe_trampoline)
