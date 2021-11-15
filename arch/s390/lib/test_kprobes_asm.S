/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/linkage.h>
#include <asm/ftrace.h>

#define KPROBES_TARGET_START(name)	\
	SYM_FUNC_START(name);		\
	FTRACE_GEN_NOP_ASM(name)

#define KPROBES_TARGET_END(name)	\
	SYM_FUNC_END(name);		\
	SYM_DATA(name##_offs, .quad 1b - name)

KPROBES_TARGET_START(kprobes_target_in_insn4)
	.word 0x4700 // bc 0,0
1:	.word 0x0000
	br %r14
KPROBES_TARGET_END(kprobes_target_in_insn4)

KPROBES_TARGET_START(kprobes_target_in_insn6_lo)
	.word 0xe310 // ly 1,0
1:	.word 0x0000
	.word 0x0058
	br %r14
KPROBES_TARGET_END(kprobes_target_in_insn6_lo)

KPROBES_TARGET_START(kprobes_target_in_insn6_hi)
	.word 0xe310 // ly 1,0
	.word 0x0000
1:	.word 0x0058
	br %r14
KPROBES_TARGET_END(kprobes_target_in_insn6_hi)

KPROBES_TARGET_START(kprobes_target_bp)
	nop
	.word 0x0000
	nop
1:	br %r14
KPROBES_TARGET_END(kprobes_target_bp)

KPROBES_TARGET_START(kprobes_target_odd)
	.byte 0x07
1:	.byte 0x07
	br %r14
KPROBES_TARGET_END(kprobes_target_odd)
