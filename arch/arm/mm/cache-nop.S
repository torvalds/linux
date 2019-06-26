/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

#include "proc-macros.S"

ENTRY(nop_flush_icache_all)
	ret	lr
ENDPROC(nop_flush_icache_all)

	.globl nop_flush_kern_cache_all
	.equ nop_flush_kern_cache_all, nop_flush_icache_all

	.globl nop_flush_kern_cache_louis
	.equ nop_flush_kern_cache_louis, nop_flush_icache_all

	.globl nop_flush_user_cache_all
	.equ nop_flush_user_cache_all, nop_flush_icache_all

	.globl nop_flush_user_cache_range
	.equ nop_flush_user_cache_range, nop_flush_icache_all

	.globl nop_coherent_kern_range
	.equ nop_coherent_kern_range, nop_flush_icache_all

ENTRY(nop_coherent_user_range)
	mov	r0, 0
	ret	lr
ENDPROC(nop_coherent_user_range)

	.globl nop_flush_kern_dcache_area
	.equ nop_flush_kern_dcache_area, nop_flush_icache_all

	.globl nop_dma_flush_range
	.equ nop_dma_flush_range, nop_flush_icache_all

	.globl nop_dma_map_area
	.equ nop_dma_map_area, nop_flush_icache_all

	.globl nop_dma_unmap_area
	.equ nop_dma_unmap_area, nop_flush_icache_all

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions nop
