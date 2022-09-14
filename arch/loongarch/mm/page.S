/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/export.h>
#include <asm/page.h>
#include <asm/regdef.h>

	.align 5
SYM_FUNC_START(clear_page)
	lu12i.w	t0, 1 << (PAGE_SHIFT - 12)
	add.d	t0, t0, a0
1:
	st.d	zero, a0, 0
	st.d	zero, a0, 8
	st.d	zero, a0, 16
	st.d	zero, a0, 24
	st.d	zero, a0, 32
	st.d	zero, a0, 40
	st.d	zero, a0, 48
	st.d	zero, a0, 56
	addi.d	a0,   a0, 128
	st.d	zero, a0, -64
	st.d	zero, a0, -56
	st.d	zero, a0, -48
	st.d	zero, a0, -40
	st.d	zero, a0, -32
	st.d	zero, a0, -24
	st.d	zero, a0, -16
	st.d	zero, a0, -8
	bne	t0,   a0, 1b

	jr	ra
SYM_FUNC_END(clear_page)
EXPORT_SYMBOL(clear_page)

.align 5
SYM_FUNC_START(copy_page)
	lu12i.w	t8, 1 << (PAGE_SHIFT - 12)
	add.d	t8, t8, a0
1:
	ld.d	t0, a1, 0
	ld.d	t1, a1, 8
	ld.d	t2, a1, 16
	ld.d	t3, a1, 24
	ld.d	t4, a1, 32
	ld.d	t5, a1, 40
	ld.d	t6, a1, 48
	ld.d	t7, a1, 56

	st.d	t0, a0, 0
	st.d	t1, a0, 8
	ld.d	t0, a1, 64
	ld.d	t1, a1, 72
	st.d	t2, a0, 16
	st.d	t3, a0, 24
	ld.d	t2, a1, 80
	ld.d	t3, a1, 88
	st.d	t4, a0, 32
	st.d	t5, a0, 40
	ld.d	t4, a1, 96
	ld.d	t5, a1, 104
	st.d	t6, a0, 48
	st.d	t7, a0, 56
	ld.d	t6, a1, 112
	ld.d	t7, a1, 120
	addi.d	a0, a0, 128
	addi.d	a1, a1, 128

	st.d	t0, a0, -64
	st.d	t1, a0, -56
	st.d	t2, a0, -48
	st.d	t3, a0, -40
	st.d	t4, a0, -32
	st.d	t5, a0, -24
	st.d	t6, a0, -16
	st.d	t7, a0, -8

	bne	t8, a0, 1b
	jr	ra
SYM_FUNC_END(copy_page)
EXPORT_SYMBOL(copy_page)
