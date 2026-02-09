/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/page.h>
#include <asm/regdef.h>

	.align 5
SYM_FUNC_START(clear_page)
	lu12i.w		t0, 1 << (PAGE_SHIFT - 12)
	PTR_ADD		t0, t0, a0
1:
	LONG_S		zero, a0, (LONGSIZE * 0)
	LONG_S		zero, a0, (LONGSIZE * 1)
	LONG_S		zero, a0, (LONGSIZE * 2)
	LONG_S		zero, a0, (LONGSIZE * 3)
	LONG_S		zero, a0, (LONGSIZE * 4)
	LONG_S		zero, a0, (LONGSIZE * 5)
	LONG_S		zero, a0, (LONGSIZE * 6)
	LONG_S		zero, a0, (LONGSIZE * 7)
	PTR_ADDI	a0,   a0, (LONGSIZE * 16)
	LONG_S		zero, a0, -(LONGSIZE * 8)
	LONG_S		zero, a0, -(LONGSIZE * 7)
	LONG_S		zero, a0, -(LONGSIZE * 6)
	LONG_S		zero, a0, -(LONGSIZE * 5)
	LONG_S		zero, a0, -(LONGSIZE * 4)
	LONG_S		zero, a0, -(LONGSIZE * 3)
	LONG_S		zero, a0, -(LONGSIZE * 2)
	LONG_S		zero, a0, -(LONGSIZE * 1)
	bne		t0,   a0, 1b

	jr		ra
SYM_FUNC_END(clear_page)
EXPORT_SYMBOL(clear_page)

.align 5
SYM_FUNC_START(copy_page)
	lu12i.w		t8, 1 << (PAGE_SHIFT - 12)
	PTR_ADD		t8, t8, a0
1:
	LONG_L		t0, a1, (LONGSIZE * 0)
	LONG_L		t1, a1, (LONGSIZE * 1)
	LONG_L		t2, a1, (LONGSIZE * 2)
	LONG_L		t3, a1, (LONGSIZE * 3)
	LONG_L		t4, a1, (LONGSIZE * 4)
	LONG_L		t5, a1, (LONGSIZE * 5)
	LONG_L		t6, a1, (LONGSIZE * 6)
	LONG_L		t7, a1, (LONGSIZE * 7)

	LONG_S		t0, a0, (LONGSIZE * 0)
	LONG_S		t1, a0, (LONGSIZE * 1)
	LONG_L		t0, a1, (LONGSIZE * 8)
	LONG_L		t1, a1, (LONGSIZE * 9)
	LONG_S		t2, a0, (LONGSIZE * 2)
	LONG_S		t3, a0, (LONGSIZE * 3)
	LONG_L		t2, a1, (LONGSIZE * 10)
	LONG_L		t3, a1, (LONGSIZE * 11)
	LONG_S		t4, a0, (LONGSIZE * 4)
	LONG_S		t5, a0, (LONGSIZE * 5)
	LONG_L		t4, a1, (LONGSIZE * 12)
	LONG_L		t5, a1, (LONGSIZE * 13)
	LONG_S		t6, a0, (LONGSIZE * 6)
	LONG_S		t7, a0, (LONGSIZE * 7)
	LONG_L		t6, a1, (LONGSIZE * 14)
	LONG_L		t7, a1, (LONGSIZE * 15)
	PTR_ADDI	a0, a0, (LONGSIZE * 16)
	PTR_ADDI	a1, a1, (LONGSIZE * 16)

	LONG_S		t0, a0, -(LONGSIZE * 8)
	LONG_S		t1, a0, -(LONGSIZE * 7)
	LONG_S		t2, a0, -(LONGSIZE * 6)
	LONG_S		t3, a0, -(LONGSIZE * 5)
	LONG_S		t4, a0, -(LONGSIZE * 4)
	LONG_S		t5, a0, -(LONGSIZE * 3)
	LONG_S		t6, a0, -(LONGSIZE * 2)
	LONG_S		t7, a0, -(LONGSIZE * 1)

	bne		t8, a0, 1b
	jr		ra
SYM_FUNC_END(copy_page)
EXPORT_SYMBOL(copy_page)
