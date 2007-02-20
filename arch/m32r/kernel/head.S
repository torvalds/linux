/*
 *  linux/arch/m32r/kernel/head.S
 *
 *  M32R startup code.
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto
 */

#include <linux/init.h>
__INIT
__INITDATA

	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/assembler.h>
#include <asm/m32r.h>
#include <asm/mmu_context.h>

/*
 * References to members of the boot_cpu_data structure.
 */
	.text
	.global	start_kernel
	.global __bss_start
	.global _end
ENTRY(stext)
ENTRY(_stext)
ENTRY(startup_32)
	/* Setup up the stack pointer */
	LDIMM	(r0, spi_stack_top)
	LDIMM	(r1, spu_stack_top)
	mvtc	r0, spi
	mvtc	r1, spu

	/* Initilalize PSW */
	ldi	r0, #0x0000		/* use SPI, disable EI */
	mvtc	r0, psw

	/* Set up the stack pointer */
	LDIMM	(r0, stack_start)
	ld	r0, @r0
	mvtc	r0, spi

/*
 * Clear BSS first so that there are no surprises...
 */
#ifdef CONFIG_ISA_DUAL_ISSUE

	LDIMM	(r2, __bss_start)
	LDIMM	(r3, _end)
	sub	r3, r2		; BSS size in bytes
	; R4 = BSS size in longwords (rounded down)
	mv	r4, r3		    ||	ldi	r1, #0
	srli	r4, #4		    ||	addi	r2, #-4
	beqz	r4, .Lendloop1
.Lloop1:
#ifndef CONFIG_CHIP_M32310
	; Touch memory for the no-write-allocating cache.
	ld	r0, @(4,r2)
#endif
	st	r1, @+r2	    ||	addi	r4, #-1
	st	r1, @+r2
	st	r1, @+r2
	st	r1, @+r2	    ||	cmpeq	r1, r4	; R4 = 0?
	bnc	.Lloop1
.Lendloop1:
	and3	r4, r3, #15
	addi	r2, #4
	beqz	r4, .Lendloop2
.Lloop2:
	stb	r1, @r2		    ||	addi	r4, #-1
	addi	r2, #1
	bnez	r4, .Lloop2
.Lendloop2:

#else /* not CONFIG_ISA_DUAL_ISSUE */

	LDIMM	(r2, __bss_start)
	LDIMM	(r3, _end)
	sub	r3, r2		; BSS size in bytes
	mv	r4, r3
	srli	r4, #2		; R4 = BSS size in longwords (rounded down)
	ldi	r1, #0		; clear R1 for longwords store
	addi	r2, #-4		; account for pre-inc store
	beqz	r4, .Lendloop1	; any more to go?
.Lloop1:
	st	r1, @+r2	; yep, zero out another longword
	addi	r4, #-1		; decrement count
	bnez	r4, .Lloop1	; go do some more
.Lendloop1:
	and3	r4, r3, #3	; get no. of remaining BSS bytes to clear
	addi	r2, #4		; account for pre-inc store
	beqz	r4, .Lendloop2	; any more to go?
.Lloop2:
	stb	r1, @r2		; yep, zero out another byte
	addi	r2, #1		; bump address
	addi	r4, #-1		; decrement count
	bnez	r4, .Lloop2	; go do some more
.Lendloop2:

#endif /* not CONFIG_ISA_DUAL_ISSUE */

#if 0  /* M32R_FIXME */
/*
 * Copy data segment from ROM to RAM.
 */
	.global ROM_D, TOP_DATA, END_DATA

	LDIMM	(r1, ROM_D)
	LDIMM	(r2, TOP_DATA)
	LDIMM	(r3, END_DATA)
	addi	r2, #-4
	addi	r3, #-4
loop1:
	ld	r0, @r1+
	st	r0, @+r2
	cmp	r2, r3
	bc	loop1
#endif /* 0 */

/* Jump to kernel */
	LDIMM	(r2, start_kernel)
	jl	r2
	.fillinsn
1:
	bra	1b		; main should never return here, but
				; just in case, we know what happens.

#ifdef CONFIG_SMP
/*
 * AP startup routine
 */
	.text
	.global	eit_vector
ENTRY(startup_AP)
;; setup EVB
	LDIMM  (r4, eit_vector)
	mvtc   r4, cr5

;; enable MMU
	LDIMM	(r2, init_tlb)
	jl	r2
	seth  r4, #high(MATM)
	or3   r4, r4, #low(MATM)
	ldi   r5, #0x01
	st    r5, @r4            ; Set MATM Reg(T bit ON)
	ld    r6, @r4            ; MATM Check
	LDIMM (r5, 1f)
	jmp   r5                 ; enable MMU
	nop
	.fillinsn
1:
;; ISN check
	ld    r6, @r4            ; MATM Check
	seth  r4, #high(M32R_ICU_ISTS_ADDR)
	or3   r4, r4, #low(M32R_ICU_ISTS_ADDR)
	ld    r5, @r4		; Read ISTSi reg.
	mv    r6, r5
	slli  r5, #13  ; PIML check
	srli  r5, #13  ;
	seth  r4, #high(M32R_ICU_IMASK_ADDR)
	or3   r4, r4, #low(M32R_ICU_IMASK_ADDR)
	st    r5, @r4		; Write IMASKi reg.
	slli  r6, #4   ; ISN check
	srli  r6, #26  ;
	seth  r4, #high(M32R_IRQ_IPI5)
	or3   r4, r4, #low(M32R_IRQ_IPI5)
	bne   r4, r6, 2f  ; if (ISN != CPU_BOOT_IPI) goto sleep;

;; check cpu_bootout_map and set cpu_bootin_map
	LDIMM (r4, cpu_bootout_map)
	ld    r4, @r4
	seth  r5, #high(M32R_CPUID_PORTL)
	or3   r5, r5, #low(M32R_CPUID_PORTL)
	ld    r5, @r5
	ldi   r6, #1
	sll   r6, r5
	and   r4, r6
	beqz  r4, 2f
	LDIMM (r4, cpu_bootin_map)
	ld    r5, @r4
	or    r5, r6
	st    r6, @r4

;; clear PSW
	ldi   r4, #0
	mvtc  r4, psw

;; setup SPI
	LDIMM (r4, stack_start)
	ld    r4, @r4
	mvtc  r4, spi

;; setup BPC (start_secondary)
	LDIMM (r4, start_secondary)
	mvtc  r4, bpc

	rte  ; goto startup_secondary
	nop
	nop

	.fillinsn
2:
	;; disable MMU
	seth  r4, #high(MATM)
	or3   r4, r4, #low(MATM)
	ldi   r5, #0
    	st    r5, @r4            ; Set MATM Reg(T bit OFF)
    	ld    r6, @r4            ; MATM Check
	LDIMM (r4, 3f)
	seth  r5, #high(__PAGE_OFFSET)
	or3   r5, r5, #low(__PAGE_OFFSET)
	not   r5, r5
	and   r4, r5
	jmp   r4                 ; disable MMU
	nop
	.fillinsn
3:
	;; SLEEP and wait IPI
	LDIMM (r4, AP_loop)
	seth  r5, #high(__PAGE_OFFSET)
	or3   r5, r5, #low(__PAGE_OFFSET)
	not   r5, r5
	and   r4, r5
	jmp   r4
	nop
	nop
#endif  /* CONFIG_SMP */

ENTRY(stack_start)
	.long	init_thread_union+8192
	.long	__KERNEL_DS

/*
 * This is initialized to create a identity-mapping at 0-4M (for bootup
 * purposes) and another mapping of the 0-4M area at virtual address
 * PAGE_OFFSET.
 */
	.text

#define  MOUNT_ROOT_RDONLY    1
#define  RAMDISK_FLAGS        0		; 1024KB
#define  ORIG_ROOT_DEV        0x0100	; /dev/ram0 (major:01, minor:00)
#define  LOADER_TYPE          1		; (??? - non-zero value seems
					; to be needed to boot from initrd)

#define  COMMAND_LINE ""

	.section	.empty_zero_page, "aw"
ENTRY(empty_zero_page)
	.long	MOUNT_ROOT_RDONLY		/* offset: +0x00 */
	.long	RAMDISK_FLAGS
	.long	ORIG_ROOT_DEV
	.long	LOADER_TYPE
	.long	0	/* INITRD_START */	/* +0x10 */
	.long	0	/* INITRD_SIZE */
	.long	0	/* CPU_CLOCK */
	.long	0	/* BUS_CLOCK */
	.long	0	/* TIMER_DIVIDE */	/* +0x20 */
	.balign	256,0
	.asciz	COMMAND_LINE
  	.byte	0
	.balign	4096,0,4096

/*------------------------------------------------------------------------
 * Stack area
 */
	.section .spi
	ALIGN
	.global spi_stack_top
	.zero	1024
spi_stack_top:

	.section .spu
	ALIGN
	.global spu_stack_top
	.zero	1024
spu_stack_top:

	.end
