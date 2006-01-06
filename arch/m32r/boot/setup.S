/*
 *  linux/arch/m32r/boot/setup.S -- A setup code.
 *
 *  Copyright (C) 2001-2005   Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto, Hayato Fujiwara
 *
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/config.h>
#include <asm/assembler.h>
#include <asm/mmu_context.h>
#include <asm/m32r.h>

/*
 * References to members of the boot_cpu_data structure.
 */

#define CPU_PARAMS	boot_cpu_data
#define M32R_MCICAR	 0xfffffff0
#define M32R_MCDCAR	 0xfffffff4
#define M32R_MCCR 	 0xfffffffc
#define M32R_BSCR0	 0xffffffd2

;BSEL
#define BSEL0CR0	 0x00ef5000
#define	BSEL0CR1	 0x00ef5004
#define BSEL1CR0	 0x00ef5100
#define BSEL1CR1	 0x00ef5104
#define BSEL0CR0_VAL	 0x00000000
#define BSEL0CR1_VAL	 0x01200100
#define BSEL1CR0_VAL	 0x01018000
#define BSEL1CR1_VAL	 0x00200001

;SDRAMC
#define SDRAMC_SDRF0	 0x00ef6000
#define SDRAMC_SDRF1	 0x00ef6004
#define SDRAMC_SDIR0	 0x00ef6008
#define SDRAMC_SDIR1	 0x00ef600c
#define SDRAMC_SD0ADR	 0x00ef6020
#define SDRAMC_SD0ER	 0x00ef6024
#define SDRAMC_SD0TR	 0x00ef6028
#define SDRAMC_SD0MOD	 0x00ef602c
#define SDRAMC_SD1ADR	 0x00ef6040
#define SDRAMC_SD1ER	 0x00ef6044
#define SDRAMC_SD1TR	 0x00ef6048
#define SDRAMC_SD1MOD	 0x00ef604c
#define SDRAM0		 0x18000000
#define SDRAM1		 0x1c000000

/*------------------------------------------------------------------------
 * start up
 */

/*------------------------------------------------------------------------
 * Kernel entry
 */
	.section .boot, "ax"
ENTRY(boot)

/* Set cache mode */
#if defined(CONFIG_CHIP_XNUX2)
	ldi	r0, #-2              ;LDIMM	(r0, M32R_MCCR)
	ldi	r1, #0x0101		; cache on (with invalidation)
;	ldi	r1, #0x00		; cache off
	sth	r1, @r0
#elif defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_VDEC2) \
    || defined(CONFIG_CHIP_OPSP)
	ldi	r0, #-4              ;LDIMM	(r0, M32R_MCCR)
	ldi	r1, #0x73		; cache on (with invalidation)
;	ldi	r1, #0x00		; cache off
	st	r1, @r0
#elif defined(CONFIG_CHIP_M32102)
	ldi	r0, #-4              ;LDIMM	(r0, M32R_MCCR)
	ldi	r1, #0x101		; cache on (with invalidation)
;	ldi	r1, #0x00		; cache off
	st	r1, @r0
#elif defined(CONFIG_CHIP_M32104)
	ldi	r0, #-96		; DNCR0
	seth	r1, #0x0060		;  from 0x00600000
	or3	r1, r1, #0x0005		;  size 2MB
	st	r1, @r0
	seth	r1, #0x0100		;  from 0x01000000
	or3	r1, r1, #0x0003		;  size 16MB
	st	r1, @+r0
	seth	r1, #0x0200		;  from 0x02000000
	or3	r1, r1, #0x0002		;  size 32MB
	st	r1, @+r0
	ldi	r0, #-4              ;LDIMM	(r0, M32R_MCCR)
	ldi	r1, #0x703		; cache on (with invalidation)
	st	r1, @r0
#else
#error unknown chip configuration
#endif

#ifdef CONFIG_SMP
	;; if not BSP (CPU#0) goto AP_loop
	seth	r5, #shigh(M32R_CPUID_PORTL)
	ld      r5, @(low(M32R_CPUID_PORTL), r5)
	bnez	r5, AP_loop
#if !defined(CONFIG_PLAT_USRV)
	;; boot AP
	ld24	r5, #0xeff2f8		; IPICR7
	ldi	r6, #0x2		; IPI to CPU1
	st	r6, @r5
#endif
#endif

/*
 *  Now, Jump to stext
 *        if with MMU,    TLB on.
 *        if with no MMU, only jump.
 */
 	.global	eit_vector
mmu_on:
	LDIMM	(r13, stext)
#ifdef CONFIG_MMU
	bl	init_tlb
	LDIMM	(r2, eit_vector)		; set EVB(cr5)
	mvtc    r2, cr5
	seth	r0, #high(MMU_REG_BASE)		; Set MMU_REG_BASE higher
	or3     r0, r0, #low(MMU_REG_BASE)	; Set MMU_REG_BASE lower
	ldi     r1, #0x01
	st      r1, @(MATM_offset,r0)		; Set MATM (T bit ON)
	ld      r0, @(MATM_offset,r0)		; Check
#else
#if defined(CONFIG_CHIP_M32700)
	seth	r0,#high(M32R_MCDCAR)
	or3	r0,r0,#low(M32R_MCDCAR)
	ld24	r1,#0x8080
	st	r1,@r0
#elif defined(CONFIG_CHIP_M32104)
	LDIMM	(r2, eit_vector)		; set EVB(cr5)
	mvtc    r2, cr5
#endif
#endif	/* CONFIG_MMU */
	jmp	r13
	nop
	nop

#ifdef CONFIG_SMP
/*
 * AP wait loop
 */
ENTRY(AP_loop)
	;; disable interrupt
	clrpsw	#0x40
	;; reset EVB
	LDIMM	(r4, _AP_RE)
	seth	r5, #high(__PAGE_OFFSET)
	or3	r5, r5, #low(__PAGE_OFFSET)
	not	r5, r5
	and	r4, r5
	mvtc	r4, cr5
	;; disable maskable interrupt
	seth	r4, #high(M32R_ICU_IMASK_PORTL)
	or3	r4, r4, #low(M32R_ICU_IMASK_PORTL)
	ldi	r5, #0
	st	r5, @r4
	ld	r5, @r4
	;; enable only IPI
	setpsw	#0x40
    	;; LOOOOOOOOOOOOOOP!!!
	.fillinsn
2:
	nop
	nop
	bra	2b
	nop
	nop

#ifdef CONFIG_CHIP_M32700_TS1
	.global	dcache_dummy
	.balign	16, 0
dcache_dummy:
	.byte	16
#endif	/* CONFIG_CHIP_M32700_TS1 */
#endif	/* CONFIG_SMP */

	.end

