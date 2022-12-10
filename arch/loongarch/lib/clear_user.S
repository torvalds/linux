/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/export.h>
#include <asm/regdef.h>

.irp to, 0
.L_fixup_handle_\to\():
	addi.d	a0, a1, (\to) * (-8)
	jr	ra
.endr

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
	bgtz	a1, 1b

2:	move	a0, a1
	jr	ra

	_asm_extable 1b, .L_fixup_handle_0
SYM_FUNC_END(__clear_user)

EXPORT_SYMBOL(__clear_user)
