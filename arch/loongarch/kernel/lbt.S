/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Qi Hu <huqi@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/regdef.h>
#include <asm/unwind_hints.h>

#define SCR_REG_WIDTH 8

	.macro	EX insn, reg, src, offs
.ex\@:	\insn	\reg, \src, \offs
	_asm_extable .ex\@, .L_lbt_fault
	.endm

/*
 * Save a thread's lbt context.
 */
SYM_FUNC_START(_save_lbt)
	movscr2gr	t1, $scr0		# save scr
	stptr.d		t1, a0, THREAD_SCR0
	movscr2gr	t1, $scr1
	stptr.d		t1, a0, THREAD_SCR1
	movscr2gr	t1, $scr2
	stptr.d		t1, a0, THREAD_SCR2
	movscr2gr	t1, $scr3
	stptr.d		t1, a0, THREAD_SCR3

	x86mfflag	t1, 0x3f		# save eflags
	stptr.d		t1, a0, THREAD_EFLAGS
	jr		ra
SYM_FUNC_END(_save_lbt)
EXPORT_SYMBOL(_save_lbt)

/*
 * Restore a thread's lbt context.
 */
SYM_FUNC_START(_restore_lbt)
	ldptr.d		t1, a0, THREAD_SCR0	# restore scr
	movgr2scr	$scr0, t1
	ldptr.d		t1, a0, THREAD_SCR1
	movgr2scr	$scr1, t1
	ldptr.d		t1, a0, THREAD_SCR2
	movgr2scr	$scr2, t1
	ldptr.d		t1, a0, THREAD_SCR3
	movgr2scr	$scr3, t1

	ldptr.d		t1, a0, THREAD_EFLAGS	# restore eflags
	x86mtflag	t1, 0x3f
	jr		ra
SYM_FUNC_END(_restore_lbt)
EXPORT_SYMBOL(_restore_lbt)

/*
 * Load scr/eflag with zero.
 */
SYM_FUNC_START(_init_lbt)
	movgr2scr	$scr0, zero
	movgr2scr	$scr1, zero
	movgr2scr	$scr2, zero
	movgr2scr	$scr3, zero

	x86mtflag	zero, 0x3f
	jr		ra
SYM_FUNC_END(_init_lbt)

/*
 * a0: scr
 * a1: eflag
 */
SYM_FUNC_START(_save_lbt_context)
	movscr2gr	t1, $scr0		# save scr
	EX	st.d	t1, a0, (0 * SCR_REG_WIDTH)
	movscr2gr	t1, $scr1
	EX	st.d	t1, a0, (1 * SCR_REG_WIDTH)
	movscr2gr	t1, $scr2
	EX	st.d	t1, a0, (2 * SCR_REG_WIDTH)
	movscr2gr	t1, $scr3
	EX	st.d	t1, a0, (3 * SCR_REG_WIDTH)

	x86mfflag	t1, 0x3f		# save eflags
	EX 	st.w	t1, a1, 0
	li.w		a0, 0			# success
	jr		ra
SYM_FUNC_END(_save_lbt_context)

/*
 * a0: scr
 * a1: eflag
 */
SYM_FUNC_START(_restore_lbt_context)
	EX	ld.d	t1, a0, (0 * SCR_REG_WIDTH)	# restore scr
	movgr2scr	$scr0, t1
	EX	ld.d	t1, a0, (1 * SCR_REG_WIDTH)
	movgr2scr	$scr1, t1
	EX	ld.d	t1, a0, (2 * SCR_REG_WIDTH)
	movgr2scr	$scr2, t1
	EX	ld.d	t1, a0, (3 * SCR_REG_WIDTH)
	movgr2scr	$scr3, t1

	EX 	ld.w	t1, a1, 0			# restore eflags
	x86mtflag	t1, 0x3f
	li.w		a0, 0			# success
	jr		ra
SYM_FUNC_END(_restore_lbt_context)

/*
 * a0: ftop
 */
SYM_FUNC_START(_save_ftop_context)
	x86mftop	t1
	st.w		t1, a0, 0
	li.w		a0, 0			# success
	jr		ra
SYM_FUNC_END(_save_ftop_context)

/*
 * a0: ftop
 */
SYM_FUNC_START(_restore_ftop_context)
	ld.w		t1, a0, 0
	andi		t1, t1, 0x7
	la.pcrel	a0, 1f
	alsl.d		a0, t1, a0, 3
	jr		a0
1:
	x86mttop	0
	b	2f
	x86mttop	1
	b	2f
	x86mttop	2
	b	2f
	x86mttop	3
	b	2f
	x86mttop	4
	b	2f
	x86mttop	5
	b	2f
	x86mttop	6
	b	2f
	x86mttop	7
2:
	li.w		a0, 0			# success
	jr		ra
SYM_FUNC_END(_restore_ftop_context)

.L_lbt_fault:
	li.w		a0, -EFAULT		# failure
	jr		ra

STACK_FRAME_NON_STANDARD _restore_ftop_context
