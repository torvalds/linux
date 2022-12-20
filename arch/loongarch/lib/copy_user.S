/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/cpu.h>
#include <asm/export.h>
#include <asm/regdef.h>

.irp to, 0, 1, 2, 3, 4, 5, 6, 7
.L_fixup_handle_\to\():
	addi.d	a0, a2, (\to) * (-8)
	jr	ra
.endr

SYM_FUNC_START(__copy_user)
	/*
	 * Some CPUs support hardware unaligned access
	 */
	ALTERNATIVE	"b __copy_user_generic",	\
			"b __copy_user_fast", CPU_FEATURE_UAL
SYM_FUNC_END(__copy_user)

EXPORT_SYMBOL(__copy_user)

/*
 * unsigned long __copy_user_generic(void *to, const void *from, size_t n)
 *
 * a0: to
 * a1: from
 * a2: n
 */
SYM_FUNC_START(__copy_user_generic)
	beqz	a2, 3f

1:	ld.b	t0, a1, 0
2:	st.b	t0, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1
	bgtz	a2, 1b

3:	move	a0, a2
	jr	ra

	_asm_extable 1b, .L_fixup_handle_0
	_asm_extable 2b, .L_fixup_handle_0
SYM_FUNC_END(__copy_user_generic)

/*
 * unsigned long __copy_user_fast(void *to, const void *from, unsigned long n)
 *
 * a0: to
 * a1: from
 * a2: n
 */
SYM_FUNC_START(__copy_user_fast)
	beqz	a2, 19f

	ori	a3, zero, 64
	blt	a2, a3, 17f

	/* copy 64 bytes at a time */
1:	ld.d	t0, a1, 0
2:	ld.d	t1, a1, 8
3:	ld.d	t2, a1, 16
4:	ld.d	t3, a1, 24
5:	ld.d	t4, a1, 32
6:	ld.d	t5, a1, 40
7:	ld.d	t6, a1, 48
8:	ld.d	t7, a1, 56
9:	st.d	t0, a0, 0
10:	st.d	t1, a0, 8
11:	st.d	t2, a0, 16
12:	st.d	t3, a0, 24
13:	st.d	t4, a0, 32
14:	st.d	t5, a0, 40
15:	st.d	t6, a0, 48
16:	st.d	t7, a0, 56

	addi.d	a0, a0, 64
	addi.d	a1, a1, 64
	addi.d	a2, a2, -64
	bge	a2, a3, 1b

	beqz	a2, 19f

	/* copy the remaining bytes */
17:	ld.b	t0, a1, 0
18:	st.b	t0, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1
	bgt	a2, zero, 17b

	/* return */
19:	move	a0, a2
	jr	ra

	/* fixup and ex_table */
	_asm_extable 1b, .L_fixup_handle_0
	_asm_extable 2b, .L_fixup_handle_1
	_asm_extable 3b, .L_fixup_handle_2
	_asm_extable 4b, .L_fixup_handle_3
	_asm_extable 5b, .L_fixup_handle_4
	_asm_extable 6b, .L_fixup_handle_5
	_asm_extable 7b, .L_fixup_handle_6
	_asm_extable 8b, .L_fixup_handle_7
	_asm_extable 9b, .L_fixup_handle_0
	_asm_extable 10b, .L_fixup_handle_1
	_asm_extable 11b, .L_fixup_handle_2
	_asm_extable 12b, .L_fixup_handle_3
	_asm_extable 13b, .L_fixup_handle_4
	_asm_extable 14b, .L_fixup_handle_5
	_asm_extable 15b, .L_fixup_handle_6
	_asm_extable 16b, .L_fixup_handle_7
	_asm_extable 17b, .L_fixup_handle_0
	_asm_extable 18b, .L_fixup_handle_0
SYM_FUNC_END(__copy_user_fast)
