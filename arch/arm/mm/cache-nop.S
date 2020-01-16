/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

#include "proc-macros.S"

ENTRY(yesp_flush_icache_all)
	ret	lr
ENDPROC(yesp_flush_icache_all)

	.globl yesp_flush_kern_cache_all
	.equ yesp_flush_kern_cache_all, yesp_flush_icache_all

	.globl yesp_flush_kern_cache_louis
	.equ yesp_flush_kern_cache_louis, yesp_flush_icache_all

	.globl yesp_flush_user_cache_all
	.equ yesp_flush_user_cache_all, yesp_flush_icache_all

	.globl yesp_flush_user_cache_range
	.equ yesp_flush_user_cache_range, yesp_flush_icache_all

	.globl yesp_coherent_kern_range
	.equ yesp_coherent_kern_range, yesp_flush_icache_all

ENTRY(yesp_coherent_user_range)
	mov	r0, 0
	ret	lr
ENDPROC(yesp_coherent_user_range)

	.globl yesp_flush_kern_dcache_area
	.equ yesp_flush_kern_dcache_area, yesp_flush_icache_all

	.globl yesp_dma_flush_range
	.equ yesp_dma_flush_range, yesp_flush_icache_all

	.globl yesp_dma_map_area
	.equ yesp_dma_map_area, yesp_flush_icache_all

	.globl yesp_dma_unmap_area
	.equ yesp_dma_unmap_area, yesp_flush_icache_all

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions yesp
