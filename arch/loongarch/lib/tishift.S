/* SPDX-License-Identifier: GPL-2.0-only */

#include <asm/asmmacro.h>
#include <linux/export.h>
#include <linux/linkage.h>

SYM_FUNC_START(__ashlti3)
	srli.d	t2, a0, 1
	nor	t3, zero, a2
	sll.d	t1, a1, a2
	srl.d	t2, t2, t3
	andi	t0, a2, 64
	sll.d	a0, a0, a2
	or	t1, t2, t1
	maskeqz	a1, a0, t0
	masknez	a0, a0, t0
	masknez	t0, t1, t0
	or	a1, t0, a1
	jr	ra
SYM_FUNC_END(__ashlti3)
EXPORT_SYMBOL(__ashlti3)

SYM_FUNC_START(__ashrti3)
	nor	t3, zero, a2
	slli.d	t2, a1, 1
	srl.d	t1, a0, a2
	sll.d	t2, t2, t3
	andi	t0, a2, 64
	or	t1, t2, t1
	sra.d	a2, a1, a2
	srai.d	a1, a1, 63
	maskeqz	a0, a2, t0
	maskeqz	a1, a1, t0
	masknez	a2, a2, t0
	masknez	t0, t1, t0
	or	a1, a1, a2
	or	a0, t0, a0
	jr	ra
SYM_FUNC_END(__ashrti3)
EXPORT_SYMBOL(__ashrti3)

SYM_FUNC_START(__lshrti3)
	slli.d	t2, a1, 1
	nor	t3, zero, a2
	srl.d	t1, a0, a2
	sll.d	t2, t2, t3
	andi	t0, a2, 64
	srl.d	a1, a1, a2
	or	t1, t2, t1
	maskeqz	a0, a1, t0
	masknez	a1, a1, t0
	masknez	t0, t1, t0
	or	a0, t0, a0
	jr	ra
SYM_FUNC_END(__lshrti3)
EXPORT_SYMBOL(__lshrti3)
