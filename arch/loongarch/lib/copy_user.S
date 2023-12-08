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
\to:	addi.d	a0, a2, \offset
	jr	ra
	.previous
.endif
	.section __ex_table, "a"
	PTR	\from\()b, \to\()b
	.previous
.endm

/*
 * unsigned long __copy_user(void *to, const void *from, size_t n)
 *
 * a0: to
 * a1: from
 * a2: n
 */
SYM_FUNC_START(__copy_user)
	beqz	a2, 3f

1:	ld.b	t0, a1, 0
2:	st.b	t0, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1
	bgtz	a2, 1b

3:	move	a0, a2
	jr	ra

	fixup_ex 1, 4, 0, 1
	fixup_ex 2, 4, 0, 0
SYM_FUNC_END(__copy_user)

EXPORT_SYMBOL(__copy_user)
