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

.macro fill_to_64 r0
	bstrins.d \r0, \r0, 15, 8
	bstrins.d \r0, \r0, 31, 16
	bstrins.d \r0, \r0, 63, 32
.endm

SYM_FUNC_START(memset)
	/*
	 * Some CPUs support hardware unaligned access
	 */
	ALTERNATIVE	"b __memset_generic", \
			"b __memset_fast", CPU_FEATURE_UAL
SYM_FUNC_END(memset)
_ASM_NOKPROBE(memset)

EXPORT_SYMBOL(memset)

/*
 * void *__memset_generic(void *s, int c, size_t n)
 *
 * a0: s
 * a1: c
 * a2: n
 */
SYM_FUNC_START(__memset_generic)
	move	a3, a0
	beqz	a2, 2f

1:	st.b	a1, a0, 0
	addi.d	a0, a0, 1
	addi.d	a2, a2, -1
	bgt	a2, zero, 1b

2:	move	a0, a3
	jr	ra
SYM_FUNC_END(__memset_generic)
_ASM_NOKPROBE(__memset_generic)

/*
 * void *__memset_fast(void *s, int c, size_t n)
 *
 * a0: s
 * a1: c
 * a2: n
 */
SYM_FUNC_START(__memset_fast)
	move	a3, a0
	beqz	a2, 3f

	ori	a4, zero, 64
	blt	a2, a4, 2f

	/* fill a1 to 64 bits */
	fill_to_64 a1

	/* set 64 bytes at a time */
1:	st.d	a1, a0, 0
	st.d	a1, a0, 8
	st.d	a1, a0, 16
	st.d	a1, a0, 24
	st.d	a1, a0, 32
	st.d	a1, a0, 40
	st.d	a1, a0, 48
	st.d	a1, a0, 56

	addi.d	a0, a0, 64
	addi.d	a2, a2, -64
	bge	a2, a4, 1b

	beqz	a2, 3f

	/* set the remaining bytes */
2:	st.b	a1, a0, 0
	addi.d	a0, a0, 1
	addi.d	a2, a2, -1
	bgt	a2, zero, 2b

	/* return */
3:	move	a0, a3
	jr	ra
SYM_FUNC_END(__memset_fast)
_ASM_NOKPROBE(__memset_fast)
