/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/alternative-macros.h>
#include <asm-generic/export.h>
#include <asm/hwcap.h>
#include <asm/insn-def.h>
#include <asm/page.h>

#define CBOZ_ALT(order, old, new)				\
	ALTERNATIVE(old, new, 0,				\
		    ((order) << 16) | RISCV_ISA_EXT_ZICBOZ,	\
		    CONFIG_RISCV_ISA_ZICBOZ)

/* void clear_page(void *page) */
SYM_FUNC_START(clear_page)
	li	a2, PAGE_SIZE

	/*
	 * If Zicboz isn't present, or somehow has a block
	 * size larger than 4K, then fallback to memset.
	 */
	CBOZ_ALT(12, "j .Lno_zicboz", "nop")

	lw	a1, riscv_cboz_block_size
	add	a2, a0, a2
.Lzero_loop:
	CBO_zero(a0)
	add	a0, a0, a1
	CBOZ_ALT(11, "bltu a0, a2, .Lzero_loop; ret", "nop; nop")
	CBO_zero(a0)
	add	a0, a0, a1
	CBOZ_ALT(10, "bltu a0, a2, .Lzero_loop; ret", "nop; nop")
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBOZ_ALT(9, "bltu a0, a2, .Lzero_loop; ret", "nop; nop")
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBOZ_ALT(8, "bltu a0, a2, .Lzero_loop; ret", "nop; nop")
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	CBO_zero(a0)
	add	a0, a0, a1
	bltu	a0, a2, .Lzero_loop
	ret
.Lno_zicboz:
	li	a1, 0
	tail	__memset
SYM_FUNC_END(clear_page)
EXPORT_SYMBOL(clear_page)
