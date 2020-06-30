/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/mm/proc-ucv2.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>

#include "proc-macros.S"

ENTRY(cpu_proc_fin)
	stm.w	(lr), [sp-]
	mov	ip, #PSR_R_BIT | PSR_I_BIT | PRIV_MODE
	mov.a	asr, ip
	b.l	__cpuc_flush_kern_all
	ldm.w	(pc), [sp]+

/*
 *	cpu_reset(loc)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_reset)
	mov	ip, #0
	movc	p0.c5, ip, #28			@ Cache invalidate all
	nop8

	movc	p0.c6, ip, #6			@ TLB invalidate all
	nop8

	movc	ip, p0.c1, #0			@ ctrl register
	or	ip, ip, #0x2000			@ vector base address
	andn	ip, ip, #0x000f			@ ............idam
	movc	p0.c1, ip, #0			@ disable caches and mmu
	nop
	mov	pc, r0				@ jump to loc
	nop8

/*
 *	cpu_do_idle()
 *
 *	Idle the processor (eg, wait for interrupt).
 *
 *	IRQs are already disabled.
 */
ENTRY(cpu_do_idle)
	mov	r0, #0				@ PCI address
	.rept	8
	ldw	r1, [r0]
	.endr
	mov	pc, lr

ENTRY(cpu_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_LINE_DISABLE
	csub.a	r1, #MAX_AREA_SIZE
	bsg	101f
	mov	r9, #PAGE_SZ
	sub	r9, r9, #1			@ PAGE_MASK
1:	va2pa	r0, r10, r11, r12, r13		@ r10 is PA
	b	3f
2:	cand.a	r0, r9
	beq	1b
3:	movc	p0.c5, r10, #11			@ clean D entry
	nop8
	add	r0, r0, #CACHE_LINESIZE
	add	r10, r10, #CACHE_LINESIZE
	sub.a	r1, r1, #CACHE_LINESIZE
	bua	2b
	mov	pc, lr
#endif
101:	mov	ip, #0
	movc	p0.c5, ip, #10			@ Dcache clean all
	nop8

	mov	pc, lr

/*
 *	cpu_do_switch_mm(pgd_phys)
 *
 *	Set the translation table base pointer to be pgd_phys
 *
 *	- pgd_phys - physical address of new pgd
 *
 *	It is assumed that:
 *	- we are not using split page tables
 */
	.align	5
ENTRY(cpu_do_switch_mm)
	movc	p0.c2, r0, #0			@ update page table ptr
	nop8

	movc	p0.c6, ip, #6			@ TLB invalidate all
	nop8

	mov	pc, lr

/*
 *	cpu_set_pte(ptep, pte)
 *
 *	Set a level 2 translation table entry.
 *
 *	- ptep  - pointer to level 2 translation table entry
 *	- pte   - PTE value to store
 */
	.align	5
ENTRY(cpu_set_pte)
	stw	r1, [r0]
#ifndef CONFIG_CPU_DCACHE_LINE_DISABLE
	sub	r2, r0, #PAGE_OFFSET
	movc	p0.c5, r2, #11				@ Dcache clean line
	nop8
#else
	mov	ip, #0
	movc	p0.c5, ip, #10				@ Dcache clean all
	nop8
	@dcacheline_flush	r0, r2, ip
#endif
	mov	pc, lr

