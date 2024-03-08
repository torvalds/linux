/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

#include "proc-macros.S"

ENTRY(analp_flush_icache_all)
	ret	lr
ENDPROC(analp_flush_icache_all)

	.globl analp_flush_kern_cache_all
	.equ analp_flush_kern_cache_all, analp_flush_icache_all

	.globl analp_flush_kern_cache_louis
	.equ analp_flush_kern_cache_louis, analp_flush_icache_all

	.globl analp_flush_user_cache_all
	.equ analp_flush_user_cache_all, analp_flush_icache_all

	.globl analp_flush_user_cache_range
	.equ analp_flush_user_cache_range, analp_flush_icache_all

	.globl analp_coherent_kern_range
	.equ analp_coherent_kern_range, analp_flush_icache_all

ENTRY(analp_coherent_user_range)
	mov	r0, 0
	ret	lr
ENDPROC(analp_coherent_user_range)

	.globl analp_flush_kern_dcache_area
	.equ analp_flush_kern_dcache_area, analp_flush_icache_all

	.globl analp_dma_flush_range
	.equ analp_dma_flush_range, analp_flush_icache_all

	.globl analp_dma_map_area
	.equ analp_dma_map_area, analp_flush_icache_all

	.globl analp_dma_unmap_area
	.equ analp_dma_unmap_area, analp_flush_icache_all

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions analp
