/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/linkage.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/errno.h>
#include <asm/regdef.h>

.L_fixup_handle_unaligned:
	li.w	a0, -EFAULT
	jr	ra

/*
 * unsigned long unaligned_read(void *addr, void *value, unsigned long n, bool sign)
 *
 * a0: addr
 * a1: value
 * a2: n
 * a3: sign
 */
SYM_FUNC_START(unaligned_read)
	beqz	a2, 5f

	li.w	t2, 0
	addi.d	t0, a2, -1
	slli.d	t1, t0, 3
	add.d 	a0, a0, t0

	beqz	a3, 2f
1:	ld.b	t3, a0, 0
	b	3f

2:	ld.bu	t3, a0, 0
3:	sll.d	t3, t3, t1
	or	t2, t2, t3
	addi.d	t1, t1, -8
	addi.d	a0, a0, -1
	addi.d	a2, a2, -1
	bgtz	a2, 2b
4:	st.d	t2, a1, 0

	move	a0, a2
	jr	ra

5:	li.w    a0, -EFAULT
	jr	ra

	_asm_extable 1b, .L_fixup_handle_unaligned
	_asm_extable 2b, .L_fixup_handle_unaligned
	_asm_extable 4b, .L_fixup_handle_unaligned
SYM_FUNC_END(unaligned_read)

/*
 * unsigned long unaligned_write(void *addr, unsigned long value, unsigned long n)
 *
 * a0: addr
 * a1: value
 * a2: n
 */
SYM_FUNC_START(unaligned_write)
	beqz	a2, 3f

	li.w	t0, 0
1:	srl.d	t1, a1, t0
2:	st.b	t1, a0, 0
	addi.d	t0, t0, 8
	addi.d	a2, a2, -1
	addi.d	a0, a0, 1
	bgtz	a2, 1b

	move	a0, a2
	jr	ra

3:	li.w    a0, -EFAULT
	jr	ra

	_asm_extable 2b, .L_fixup_handle_unaligned
SYM_FUNC_END(unaligned_write)
