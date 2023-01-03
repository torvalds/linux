/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/cpu.h>
#include <asm/export.h>
#include <asm/regdef.h>

SYM_FUNC_START(memmove)
	blt	a0, a1, 1f	/* dst < src, memcpy */
	blt	a1, a0, 3f	/* src < dst, rmemcpy */
	jr	ra		/* dst == src, return */

	/* if (src - dst) < 64, copy 1 byte at a time */
1:	ori	a3, zero, 64
	sub.d	t0, a1, a0
	blt	t0, a3, 2f
	b	memcpy
2:	b	__memcpy_generic

	/* if (dst - src) < 64, copy 1 byte at a time */
3:	ori	a3, zero, 64
	sub.d	t0, a0, a1
	blt	t0, a3, 4f
	b	rmemcpy
4:	b	__rmemcpy_generic
SYM_FUNC_END(memmove)

EXPORT_SYMBOL(memmove)

SYM_FUNC_START(rmemcpy)
	/*
	 * Some CPUs support hardware unaligned access
	 */
	ALTERNATIVE	"b __rmemcpy_generic", \
			"b __rmemcpy_fast", CPU_FEATURE_UAL
SYM_FUNC_END(rmemcpy)

/*
 * void *__rmemcpy_generic(void *dst, const void *src, size_t n)
 *
 * a0: dst
 * a1: src
 * a2: n
 */
SYM_FUNC_START(__rmemcpy_generic)
	move	a3, a0
	beqz	a2, 2f

	add.d	a0, a0, a2
	add.d	a1, a1, a2

1:	ld.b	t0, a1, -1
	st.b	t0, a0, -1
	addi.d	a0, a0, -1
	addi.d	a1, a1, -1
	addi.d	a2, a2, -1
	bgt	a2, zero, 1b

2:	move	a0, a3
	jr	ra
SYM_FUNC_END(__rmemcpy_generic)

/*
 * void *__rmemcpy_fast(void *dst, const void *src, size_t n)
 *
 * a0: dst
 * a1: src
 * a2: n
 */
SYM_FUNC_START(__rmemcpy_fast)
	move	a3, a0
	beqz	a2, 3f

	add.d	a0, a0, a2
	add.d	a1, a1, a2

	ori	a4, zero, 64
	blt	a2, a4, 2f

	/* copy 64 bytes at a time */
1:	ld.d	t0, a1, -8
	ld.d	t1, a1, -16
	ld.d	t2, a1, -24
	ld.d	t3, a1, -32
	ld.d	t4, a1, -40
	ld.d	t5, a1, -48
	ld.d	t6, a1, -56
	ld.d	t7, a1, -64
	st.d	t0, a0, -8
	st.d	t1, a0, -16
	st.d	t2, a0, -24
	st.d	t3, a0, -32
	st.d	t4, a0, -40
	st.d	t5, a0, -48
	st.d	t6, a0, -56
	st.d	t7, a0, -64

	addi.d	a0, a0, -64
	addi.d	a1, a1, -64
	addi.d	a2, a2, -64
	bge	a2, a4, 1b

	beqz	a2, 3f

	/* copy the remaining bytes */
2:	ld.b	t0, a1, -1
	st.b	t0, a0, -1
	addi.d	a0, a0, -1
	addi.d	a1, a1, -1
	addi.d	a2, a2, -1
	bgt	a2, zero, 2b

	/* return */
3:	move	a0, a3
	jr	ra
SYM_FUNC_END(__rmemcpy_fast)
