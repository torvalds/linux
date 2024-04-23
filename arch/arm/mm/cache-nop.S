/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>

#include "proc-macros.S"

SYM_TYPED_FUNC_START(nop_flush_icache_all)
	ret	lr
SYM_FUNC_END(nop_flush_icache_all)

SYM_TYPED_FUNC_START(nop_flush_kern_cache_all)
	ret	lr
SYM_FUNC_END(nop_flush_kern_cache_all)

	.globl nop_flush_kern_cache_louis
	.equ nop_flush_kern_cache_louis, nop_flush_icache_all

SYM_TYPED_FUNC_START(nop_flush_user_cache_all)
	ret	lr
SYM_FUNC_END(nop_flush_user_cache_all)

SYM_TYPED_FUNC_START(nop_flush_user_cache_range)
	ret	lr
SYM_FUNC_END(nop_flush_user_cache_range)

SYM_TYPED_FUNC_START(nop_coherent_kern_range)
	ret	lr
SYM_FUNC_END(nop_coherent_kern_range)

SYM_TYPED_FUNC_START(nop_coherent_user_range)
	mov	r0, 0
	ret	lr
SYM_FUNC_END(nop_coherent_user_range)

SYM_TYPED_FUNC_START(nop_flush_kern_dcache_area)
	ret	lr
SYM_FUNC_END(nop_flush_kern_dcache_area)

SYM_TYPED_FUNC_START(nop_dma_flush_range)
	ret	lr
SYM_FUNC_END(nop_dma_flush_range)

SYM_TYPED_FUNC_START(nop_dma_map_area)
	ret	lr
SYM_FUNC_END(nop_dma_map_area)

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions nop

SYM_TYPED_FUNC_START(nop_dma_unmap_area)
	ret	lr
SYM_FUNC_END(nop_dma_unmap_area)
