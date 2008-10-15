/*
 *  linux/arch/arm/mm/arm946.S: utility functions for ARM946E-S
 *
 *  Copyright (C) 2004-2006 Hyok S. Choi (hyok.choi@samsung.com)
 *
 *  (Many of cache codes are from proc-arm926.S)
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

/*
 * ARM946E-S is synthesizable to have 0KB to 1MB sized D-Cache,
 * comprising 256 lines of 32 bytes (8 words).
 */
#define CACHE_DSIZE	(CONFIG_CPU_DCACHE_SIZE) /* typically 8KB. */
#define CACHE_DLINESIZE	32			/* fixed */
#define CACHE_DSEGMENTS	4			/* fixed */
#define CACHE_DENTRIES	(CACHE_DSIZE / CACHE_DSEGMENTS / CACHE_DLINESIZE)
#define CACHE_DLIMIT	(CACHE_DSIZE * 4)	/* benchmark needed */

	.text
/*
 * cpu_arm946_proc_init()
 * cpu_arm946_switch_mm()
 *
 * These are not required.
 */
ENTRY(cpu_arm946_proc_init)
ENTRY(cpu_arm946_switch_mm)
	mov	pc, lr

/*
 * cpu_arm946_proc_fin()
 */
ENTRY(cpu_arm946_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
	bl	arm946_flush_kern_cache_all
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x00001000		@ i-cache
	bic	r0, r0, #0x00000004		@ d-cache
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

/*
 * cpu_arm946_reset(loc)
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm946_reset)
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
 * cpu_arm946_do_idle()
 */
	.align	5
ENTRY(cpu_arm946_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr

/*
 *	flush_user_cache_all()
 */
ENTRY(arm946_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm946_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ flush D cache
#else
	mov	r1, #(CACHE_DSEGMENTS - 1) << 29 @ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 4 @ n entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean/flush D index
	subs	r3, r3, #1 << 4
	bcs	2b				@ entries n to 0
	subs	r1, r1, #1 << 29
	bcs	1b				@ segments 3 to 0
#endif
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ flush I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Clean and invalidate a range of cache entries in the
 *	specified address range.
 *
 *	- start	- start address (inclusive)
 *	- end	- end address (exclusive)
 *	- flags	- vm_flags describing address space
 * (same as arm926)
 */
ENTRY(arm946_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bhs	__flush_whole_cache

1:	tst	r2, #VM_EXEC
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
#else
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
#endif
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
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
ENTRY(arm946_coherent_kern_range)
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
 * (same as arm926)
 */
ENTRY(arm946_coherent_user_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 * (same as arm926)
 */
ENTRY(arm946_flush_kern_dcache_page)
	add	r1, r0, #PAGE_SZ
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 * (same as arm926)
 */
ENTRY(arm946_dma_inv_range)
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	tst	r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
#endif
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as arm926)
 */
ENTRY(arm946_dma_clean_range)
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as arm926)
 */
ENTRY(arm946_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
#else
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
#endif
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm946_cache_fns)
	.long	arm946_flush_kern_cache_all
	.long	arm946_flush_user_cache_all
	.long	arm946_flush_user_cache_range
	.long	arm946_coherent_kern_range
	.long	arm946_coherent_user_range
	.long	arm946_flush_kern_dcache_page
	.long	arm946_dma_inv_range
	.long	arm946_dma_clean_range
	.long	arm946_dma_flush_range


ENTRY(cpu_arm946_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

	__INIT

	.type	__arm946_setup, #function
__arm946_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c6, 0		@ invalidate D cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB

	mcr	p15, 0, r0, c6, c3, 0		@ disable memory region 3~7
	mcr	p15, 0, r0, c6, c4, 0
	mcr	p15, 0, r0, c6, c5, 0
	mcr	p15, 0, r0, c6, c6, 0
	mcr	p15, 0, r0, c6, c7, 0

	mov	r0, #0x0000003F			@ base = 0, size = 4GB
	mcr	p15, 0, r0, c6,	c0, 0		@ set region 0, default

	ldr	r0, =(CONFIG_DRAM_BASE & 0xFFFFF000) @ base[31:12] of RAM
	ldr	r1, =(CONFIG_DRAM_SIZE >> 12)	@ size of RAM (must be >= 4KB)
	mov	r2, #10				@ 11 is the minimum (4KB)
1:	add	r2, r2, #1			@ area size *= 2
	mov	r1, r1, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r2, lsl #1		@ the region register value
	orr	r0, r0, #1			@ set enable bit
	mcr	p15, 0, r0, c6,	c1, 0		@ set region 1, RAM

	ldr	r0, =(CONFIG_FLASH_MEM_BASE & 0xFFFFF000) @ base[31:12] of FLASH
	ldr	r1, =(CONFIG_FLASH_SIZE >> 12)	@ size of FLASH (must be >= 4KB)
	mov	r2, #10				@ 11 is the minimum (4KB)
1:	add	r2, r2, #1			@ area size *= 2
	mov	r1, r1, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r2, lsl #1		@ the region register value
	orr	r0, r0, #1			@ set enable bit
	mcr	p15, 0, r0, c6,	c2, 0		@ set region 2, ROM/FLASH

	mov	r0, #0x06
	mcr	p15, 0, r0, c2, c0, 0		@ region 1,2 d-cacheable
	mcr	p15, 0, r0, c2, c0, 1		@ region 1,2 i-cacheable
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mov	r0, #0x00			@ disable whole write buffer
#else
	mov	r0, #0x02			@ region 1 write bufferred
#endif
	mcr	p15, 0, r0, c3, c0, 0

/*
 *  Access Permission Settings for future permission control by PU.
 *
 *				priv.	user
 * 	region 0 (whole)	rw	--	: b0001
 * 	region 1 (RAM)		rw	rw	: b0011
 * 	region 2 (FLASH)	rw	r-	: b0010
 *	region 3~7 (none)	--	--	: b0000
 */
	mov	r0, #0x00000031
	orr	r0, r0, #0x00000200
	mcr	p15, 0, r0, c5, c0, 2		@ set data access permission
	mcr	p15, 0, r0, c5, c0, 3		@ set inst. access permission

	mrc	p15, 0, r0, c1, c0		@ get control register
	orr	r0, r0, #0x00001000		@ I-cache
	orr	r0, r0, #0x00000005		@ MPU/D-cache
#ifdef CONFIG_CPU_CACHE_ROUND_ROBIN
	orr	r0, r0, #0x00004000		@ .1.. .... .... ....
#endif
	mov	pc, lr

	.size	__arm946_setup, . - __arm946_setup

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	arm946_processor_functions, #object
ENTRY(arm946_processor_functions)
	.word	nommu_early_abort
	.word	pabort_noifar
	.word	cpu_arm946_proc_init
	.word	cpu_arm946_proc_fin
	.word	cpu_arm946_reset
	.word   cpu_arm946_do_idle

	.word	cpu_arm946_dcache_clean_area
	.word	cpu_arm946_switch_mm
	.word	0		@ cpu_*_set_pte
	.size	arm946_processor_functions, . - arm946_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv5te"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v5t"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_arm946_name, #object
cpu_arm946_name:
	.ascii	"ARM946E-S"
	.size	cpu_arm946_name, . - cpu_arm946_name

	.align

	.section ".proc.info.init", #alloc, #execinstr
	.type	__arm946_proc_info,#object
__arm946_proc_info:
	.long	0x41009460
	.long	0xff00fff0
	.long	0
	b	__arm946_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB
	.long	cpu_arm946_name
	.long	arm946_processor_functions
	.long	0
	.long	0
	.long	arm940_cache_fns
	.size	__arm946_proc_info, . - __arm946_proc_info

