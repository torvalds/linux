/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/export.h>
#include <asm/regdef.h>

.macro fixup_ex from, to, offset, fix
.if \fix
	.section .fixup, "ax"
\to:	addi.d	a0, a1, \offset
	jr	ra
	.previous
.endif
	.section __ex_table, "a"
	PTR	\from\()b, \to\()b
	.previous
.endm

/*
 * unsigned long __clear_user(void *addr, size_t size)
 *
 * a0: addr
 * a1: size
 */
SYM_FUNC_START(__clear_user)
	beqz	a1, 2f

1:	st.b	zero, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, -1
	bgt	a1, zero, 1b

2:	move	a0, a1
	jr	ra

	fixup_ex 1, 3, 0, 1
SYM_FUNC_END(__clear_user)

EXPORT_SYMBOL(__clear_user)
