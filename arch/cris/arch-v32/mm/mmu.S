/*
 *  Copyright (C) 2003 Axis Communications AB
 *
 *  Authors:	Mikael Starvik (starvik@axis.com)
 *
 * Code for the fault low-level handling routines.
 *
 */

#include <asm/page.h>
#include <asm/pgtable.h>

; Save all register. Must save in same order as struct pt_regs.
.macro SAVE_ALL
	subq	12, $sp
	move	$erp, [$sp]
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$ccs, [$sp]
	subq	4, $sp
	move	$spc, [$sp]
	subq	4, $sp
	move	$mof, [$sp]
	subq	4, $sp
	move	$srs, [$sp]
	subq	4, $sp
	move.d	$acr, [$sp]
	subq	14*4, $sp
	movem	$r13, [$sp]
	subq	4, $sp
	move.d	$r10, [$sp]
.endm

; Bus fault handler. Extracts relevant information and calls mm subsystem
; to handle the fault.
.macro	MMU_BUS_FAULT_HANDLER handler, mmu, we, ex
	.globl	\handler
\handler:
	SAVE_ALL
	move	\mmu, $srs	; Select MMU support register bank
	move.d  $sp, $r11	; regs
	moveq	1, $r12		; protection fault
	moveq   \we, $r13	; write exception?
	orq	\ex << 1, $r13	; execute?
	move    $s3, $r10	; rw_mm_cause
	and.d	~8191, $r10	; Get faulting page start address

	jsr	do_page_fault
	nop
	ba	ret_from_intr
	nop
.endm

; Refill handler. Three cases may occur:
;   1. PMD and PTE exists in mm subsystem but not in TLB
;   2. PMD exists but not PTE
;   3. PMD doesn't exist
; The code below handles case 1 and calls the mm subsystem for case 2 and 3.
; Do not touch this code without very good reasons and extensive testing.
; Note that the code is optimized to minimize stalls (makes the code harder
; to read).
;
; Each page is 8 KB. Each PMD holds 8192/4 PTEs (each PTE is 4 bytes) so each
; PMD holds 16 MB of virtual memory.
;   Bits  0-12 : Offset within a page
;   Bits 13-23 : PTE offset within a PMD
;   Bits 24-31 : PMD offset within the PGD

.macro MMU_REFILL_HANDLER handler, mmu
	.globl \handler
\handler:
	subq	4, $sp
; (The pipeline stalls for one cycle; $sp used as address in the next cycle.)
	move	$srs, [$sp]
	subq	4, $sp
	move	\mmu, $srs	; Select MMU support register bank
	move.d	$acr, [$sp]
	subq	4, $sp
	move.d	$r0, [$sp]
#ifdef CONFIG_SMP
	move    $s7, $acr	; PGD
#else
	move.d  per_cpu__current_pgd, $acr ; PGD
#endif
	; Look up PMD in PGD
	move	$s3, $r0	; rw_mm_cause
	lsrq	24, $r0	; Get PMD index into PGD (bit 24-31)
	move.d  [$acr], $acr	; PGD for the current process
	addi	$r0.d, $acr, $acr
	move	$s3, $r0	; rw_mm_cause
	move.d  [$acr], $acr	; Get PMD
	beq	1f
	; Look up PTE in PMD
	lsrq	PAGE_SHIFT, $r0
	and.w	PAGE_MASK, $acr	; Remove PMD flags
	and.d	0x7ff, $r0	; Get PTE index into PMD (bit 13-23)
	addi    $r0.d, $acr, $acr
	move.d  [$acr], $acr	; Get PTE
	beq	2f
	move.d  [$sp+], $r0	; Pop r0 in delayslot
	; Store in TLB
	move	$acr, $s5
	; Return
	move.d	[$sp+], $acr
	move  	[$sp], $srs
	addq	4, $sp
	rete
	rfe
1:	; PMD missing, let the mm subsystem fix it up.
	move.d  [$sp+], $r0	; Pop r0
2:      ; PTE missing, let the mm subsystem fix it up.
	move.d	[$sp+], $acr
	move  	[$sp], $srs
	addq	4, $sp
	SAVE_ALL
	move    \mmu, $srs
	move.d	$sp, $r11	; regs
	clear.d	$r12		; Not a protection fault
	move.w  PAGE_MASK, $acr
	move    $s3, $r10	; rw_mm_cause
	btstq   9, $r10		; Check if write access
	smi     $r13
	and.w	PAGE_MASK, $r10	; Get VPN (virtual address)
	jsr	do_page_fault
	and.w   $acr, $r10
	; Return
	ba	ret_from_intr
	nop
.endm

	; This is the MMU bus fault handlers.

MMU_REFILL_HANDLER i_mmu_refill, 1
MMU_BUS_FAULT_HANDLER i_mmu_invalid, 1, 0, 0
MMU_BUS_FAULT_HANDLER i_mmu_access,  1, 0, 0
MMU_BUS_FAULT_HANDLER i_mmu_execute, 1, 0, 1
MMU_REFILL_HANDLER d_mmu_refill,  2
MMU_BUS_FAULT_HANDLER d_mmu_invalid, 2, 0, 0
MMU_BUS_FAULT_HANDLER d_mmu_access,  2, 0, 0
MMU_BUS_FAULT_HANDLER d_mmu_write,   2, 1, 0
