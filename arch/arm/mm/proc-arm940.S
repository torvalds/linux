/*
 *  linux/arch/arm/mm/arm940.S: utility functions for ARM940T
 *
 *  Copyright (C) 2004-2006 Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include "proc-macros.S"

/* ARM940T has a 4KB DCache comprising 256 lines of 4 words */
#define CACHE_DLINESIZE	16
#define CACHE_DSEGMENTS	4
#define CACHE_DENTRIES	64

	.text
/*
 * cpu_arm940_proc_init()
 * cpu_arm940_switch_mm()
 *
 * These are not required.
 */
ENTRY(cpu_arm940_proc_init)
ENTRY(cpu_arm940_switch_mm)
	mov	pc, lr

/*
 * cpu_arm940_proc_fin()
 */
ENTRY(cpu_arm940_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x00001000		@ i-cache
	bic	r0, r0, #0x00000004		@ d-cache
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 * cpu_arm940_reset(loc)
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm940_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0		@ flush I cache
	mcr	p15, 0, ip, c7, c6, 0		@ flush D cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x00000005		@ .............c.p
	bic	ip, ip, #0x00001000		@ i-cache
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * cpu_arm940_do_idle()
 */
	.align	5
ENTRY(cpu_arm940_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr

/*
 *	flush_user_cache_all()
 */
ENTRY(arm940_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm940_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	/* FALLTHROUGH */

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	There is no efficient way to flush a range of cache entries
 *	in the specified address range. Thus, flushes all.
 *
 *	- start	- start address (inclusive)
 *	- end	- end address (exclusive)
 *	- flags	- vm_flags describing address space
 */
ENTRY(arm940_flush_user_cache_range)
	mov	ip, #0
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ flush D cache
#else
	mov	r1, #(CACHE_DSEGMENTS - 1) << 4	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean/flush D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 4
	bcs	1b				@ segments 3 to 0
#endif
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start, end.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm940_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	coherent_user_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start, end.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm940_coherent_user_range)
	/* FALLTHROUGH */

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(arm940_flush_kern_dcache_area)
	mov	ip, #0
	mov	r1, #(CACHE_DSEGMENTS - 1) << 4	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean/flush D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 4
	bcs	1b				@ segments 7 to 0
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	There is no efficient way to invalidate a specifid virtual
 *	address range. Thus, invalidates all.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
arm940_dma_inv_range:
	mov	ip, #0
	mov	r1, #(CACHE_DSEGMENTS - 1) << 4	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c6, 2		@ flush D entry
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 4
	bcs	1b				@ segments 7 to 0
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	There is no efficient way to clean a specifid virtual
 *	address range. Thus, cleans all.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
arm940_dma_clean_range:
ENTRY(cpu_arm940_dcache_clean_area)
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	mov	r1, #(CACHE_DSEGMENTS - 1) << 4	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c10, 2		@ clean D entry
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 4
	bcs	1b				@ segments 7 to 0
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	There is no efficient way to clean and invalidate a specifid
 *	virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm940_dma_flush_range)
	mov	ip, #0
	mov	r1, #(CACHE_DSEGMENTS - 1) << 4	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r3, c7, c14, 2		@ clean/flush D entry
#else
	mcr	p15, 0, r3, c7, c6, 2		@ invalidate D entry
#endif
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 4
	bcs	1b				@ segments 7 to 0
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm940_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	arm940_dma_clean_range
	bcs	arm940_dma_inv_range
	b	arm940_dma_flush_range
ENDPROC(arm940_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm940_dma_unmap_area)
	mov	pc, lr
ENDPROC(arm940_dma_unmap_area)

ENTRY(arm940_cache_fns)
	.long	arm940_flush_kern_cache_all
	.long	arm940_flush_user_cache_all
	.long	arm940_flush_user_cache_range
	.long	arm940_coherent_kern_range
	.long	arm940_coherent_user_range
	.long	arm940_flush_kern_dcache_area
	.long	arm940_dma_map_area
	.long	arm940_dma_unmap_area
	.long	arm940_dma_flush_range

	__CPUINIT

	.type	__arm940_setup, #function
__arm940_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c6, 0		@ invalidate D cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB

	mcr	p15, 0, r0, c6, c3, 0		@ disable data area 3~7
	mcr	p15, 0, r0, c6, c4, 0
	mcr	p15, 0, r0, c6, c5, 0
	mcr	p15, 0, r0, c6, c6, 0
	mcr	p15, 0, r0, c6, c7, 0

	mcr	p15, 0, r0, c6, c3, 1		@ disable instruction area 3~7
	mcr	p15, 0, r0, c6, c4, 1
	mcr	p15, 0, r0, c6, c5, 1
	mcr	p15, 0, r0, c6, c6, 1
	mcr	p15, 0, r0, c6, c7, 1

	mov	r0, #0x0000003F			@ base = 0, size = 4GB
	mcr	p15, 0, r0, c6,	c0, 0		@ set area 0, default
	mcr	p15, 0, r0, c6,	c0, 1

	ldr	r0, =(CONFIG_DRAM_BASE & 0xFFFFF000) @ base[31:12] of RAM
	ldr	r1, =(CONFIG_DRAM_SIZE >> 12)	@ size of RAM (must be >= 4KB)
	mov	r2, #10				@ 11 is the minimum (4KB)
1:	add	r2, r2, #1			@ area size *= 2
	mov	r1, r1, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r2, lsl #1		@ the area register value
	orr	r0, r0, #1			@ set enable bit
	mcr	p15, 0, r0, c6,	c1, 0		@ set area 1, RAM
	mcr	p15, 0, r0, c6,	c1, 1

	ldr	r0, =(CONFIG_FLASH_MEM_BASE & 0xFFFFF000) @ base[31:12] of FLASH
	ldr	r1, =(CONFIG_FLASH_SIZE >> 12)	@ size of FLASH (must be >= 4KB)
	mov	r2, #10				@ 11 is the minimum (4KB)
1:	add	r2, r2, #1			@ area size *= 2
	mov	r1, r1, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r2, lsl #1		@ the area register value
	orr	r0, r0, #1			@ set enable bit
	mcr	p15, 0, r0, c6,	c2, 0		@ set area 2, ROM/FLASH
	mcr	p15, 0, r0, c6,	c2, 1

	mov	r0, #0x06
	mcr	p15, 0, r0, c2, c0, 0		@ Region 1&2 cacheable
	mcr	p15, 0, r0, c2, c0, 1
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mov	r0, #0x00			@ disable whole write buffer
#else
	mov	r0, #0x02			@ Region 1 write bufferred
#endif
	mcr	p15, 0, r0, c3, c0, 0

	mov	r0, #0x10000
	sub	r0, r0, #1			@ r0 = 0xffff
	mcr	p15, 0, r0, c5, c0, 0		@ all read/write access
	mcr	p15, 0, r0, c5, c0, 1

	mrc	p15, 0, r0, c1, c0		@ get control register
	orr	r0, r0, #0x00001000		@ I-cache
	orr	r0, r0, #0x00000005		@ MPU/D-cache

	mov	pc, lr

	.size	__arm940_setup, . - __arm940_setup

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	arm940_processor_functions, #object
ENTRY(arm940_processor_functions)
	.word	nommu_early_abort
	.word	legacy_pabort
	.word	cpu_arm940_proc_init
	.word	cpu_arm940_proc_fin
	.word	cpu_arm940_reset
	.word   cpu_arm940_do_idle
	.word	cpu_arm940_dcache_clean_area
	.word	cpu_arm940_switch_mm
	.word	0		@ cpu_*_set_pte
	.size	arm940_processor_functions, . - arm940_processor_functions

	.section ".rodata"

.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4t"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_arm940_name, #object
cpu_arm940_name:
	.ascii	"ARM940T"
	.size	cpu_arm940_name, . - cpu_arm940_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__arm940_proc_info,#object
__arm940_proc_info:
	.long	0x41009400
	.long	0xff00fff0
	.long	0
	b	__arm940_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB
	.long	cpu_arm940_name
	.long	arm940_processor_functions
	.long	0
	.long	0
	.long	arm940_cache_fns
	.size	__arm940_proc_info, . - __arm940_proc_info

