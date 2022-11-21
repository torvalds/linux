/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Code copied from arch/arm64/mm/cache.S.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/alternative.h>

SYM_FUNC_START(__pi_dcache_clean_inval_poc)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__pi_dcache_clean_inval_poc)
SYM_FUNC_ALIAS(dcache_clean_inval_poc, __pi_dcache_clean_inval_poc)
