/*
 *  linux/arch/arm/mm/arm740.S: utility functions for ARM740
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
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

	.text
/*
 * cpu_arm740_proc_init()
 * cpu_arm740_do_idle()
 * cpu_arm740_dcache_clean_area()
 * cpu_arm740_switch_mm()
 *
 * These are not required.
 */
ENTRY(cpu_arm740_proc_init)
ENTRY(cpu_arm740_do_idle)
ENTRY(cpu_arm740_dcache_clean_area)
ENTRY(cpu_arm740_switch_mm)
	ret	lr

/*
 * cpu_arm740_proc_fin()
 */
ENTRY(cpu_arm740_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #0x3f000000		@ bank/f/lock/s
	bic	r0, r0, #0x0000000c		@ w-buffer/cache
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ret	lr

/*
 * cpu_arm740_reset(loc)
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_arm740_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c0, 0		@ invalidate cache
	mrc	p15, 0, ip, c1, c0, 0		@ get ctrl register
	bic	ip, ip, #0x0000000c		@ ............wc..
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	ret	r0
ENDPROC(cpu_arm740_reset)
	.popsection

	.type	__arm740_setup, #function
__arm740_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c0, 0		@ invalidate caches

	mcr	p15, 0, r0, c6, c3		@ disable area 3~7
	mcr	p15, 0, r0, c6, c4
	mcr	p15, 0, r0, c6, c5
	mcr	p15, 0, r0, c6, c6
	mcr	p15, 0, r0, c6, c7

	mov	r0, #0x0000003F			@ base = 0, size = 4GB
	mcr	p15, 0, r0, c6,	c0		@ set area 0, default

	ldr	r0, =(CONFIG_DRAM_BASE & 0xFFFFF000) @ base[31:12] of RAM
	ldr	r3, =(CONFIG_DRAM_SIZE >> 12)	@ size of RAM (must be >= 4KB)
	mov	r4, #10				@ 11 is the minimum (4KB)
1:	add	r4, r4, #1			@ area size *= 2
	movs	r3, r3, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r4, lsl #1		@ the area register value
	orr	r0, r0, #1			@ set enable bit
	mcr	p15, 0, r0, c6,	c1		@ set area 1, RAM

	ldr	r0, =(CONFIG_FLASH_MEM_BASE & 0xFFFFF000) @ base[31:12] of FLASH
	ldr	r3, =(CONFIG_FLASH_SIZE >> 12)	@ size of FLASH (must be >= 4KB)
	cmp	r3, #0
	moveq	r0, #0
	beq	2f
	mov	r4, #10				@ 11 is the minimum (4KB)
1:	add	r4, r4, #1			@ area size *= 2
	movs	r3, r3, lsr #1
	bne	1b				@ count not zero r-shift
	orr	r0, r0, r4, lsl #1		@ the area register value
	orr	r0, r0, #1			@ set enable bit
2:	mcr	p15, 0, r0, c6,	c2		@ set area 2, ROM/FLASH

	mov	r0, #0x06
	mcr	p15, 0, r0, c2, c0		@ Region 1&2 cacheable
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mov	r0, #0x00			@ disable whole write buffer
#else
	mov	r0, #0x02			@ Region 1 write bufferred
#endif
	mcr	p15, 0, r0, c3, c0

	mov	r0, #0x10000
	sub	r0, r0, #1			@ r0 = 0xffff
	mcr	p15, 0, r0, c5, c0		@ all read/write access

	mrc	p15, 0, r0, c1, c0		@ get control register
	bic	r0, r0, #0x3F000000		@ set to standard caching mode
						@ need some benchmark
	orr	r0, r0, #0x0000000d		@ MPU/Cache/WB

	ret	lr

	.size	__arm740_setup, . - __arm740_setup

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions arm740, dabort=v4t_late_abort, pabort=legacy_pabort, nommu=1

	.section ".rodata"

	string	cpu_arch_name, "armv4"
	string	cpu_elf_name, "v4"
	string	cpu_arm740_name, "ARM740T"

	.align

	.section ".proc.info.init", #alloc
	.type	__arm740_proc_info,#object
__arm740_proc_info:
	.long	0x41807400
	.long	0xfffffff0
	.long	0
	.long	0
	initfn	__arm740_setup, __arm740_proc_info
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB | HWCAP_26BIT
	.long	cpu_arm740_name
	.long	arm740_processor_functions
	.long	0
	.long	0
	.long	v4_cache_fns			@ cache model
	.size	__arm740_proc_info, . - __arm740_proc_info
