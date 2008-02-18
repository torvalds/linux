; WARNING : The refill handler has been modified, see below !!!

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
; WARNING !!!
; Modified by Mikael Asker 060725: added a workaround for strange TLB
; behavior. If the same PTE is present in more than one set, the TLB
; doesn't recognize it and we get stuck in a loop of refill exceptions.
; The workaround detects such loops and exits them by flushing
; the TLB contents. The problem and workaround were verified
; in VCS by Mikael Starvik.
;
; Each page is 8 KB. Each PMD holds 8192/4 PTEs (each PTE is 4 bytes) so each
; PMD holds 16 MB of virtual memory.
;   Bits  0-12 : Offset within a page
;   Bits 13-23 : PTE offset within a PMD
;   Bits 24-31 : PMD offset within the PGD

.macro MMU_REFILL_HANDLER handler, mmu
	.data
1:	.dword	0		; refill_count
                                ;   == 0 <=> last_refill_cause is invalid
2:	.dword	0		; last_refill_cause
	.text
	.globl \handler
\handler:
	subq	4, $sp
; (The pipeline stalls for one cycle; $sp used as address in the next cycle.)
	move	$srs, [$sp]
	subq	4, $sp
	move	\mmu, $srs	; Select MMU support register bank
	move.d	$acr, [$sp]
	subq	12, $sp
	move.d	1b, $acr        ; Point to refill_count
	movem	$r2, [$sp]

	test.d	[$acr]	        ; refill_count == 0 ?
	beq	5f		;   yes, last_refill_cause is invalid
        move.d	$acr, $r1

	; last_refill_cause is valid, investigate cause
        addq    4, $r1          ; Point to last_refill_cause
	move	$s3, $r0	; Get rw_mm_cause
	move.d	[$r1], $r2	; Get last_refill_cause
	cmp.d	$r0, $r2	; rw_mm_cause == last_refill_cause ?
	beq	6f		;   yes, increment count
	moveq	1, $r2

        ; rw_mm_cause != last_refill_cause
	move.d	$r2, [$acr]	; refill_count = 1
	move.d	$r0, [$r1]	; last_refill_cause = rw_mm_cause

3:	; Probably not in a loop, continue normal processing
#ifdef CONFIG_SMP
	move    $s7, $acr	; PGD
#else
	move.d  per_cpu__current_pgd, $acr ; PGD
#endif
	; Look up PMD in PGD
	lsrq	24, $r0	; Get PMD index into PGD (bit 24-31)
	move.d  [$acr], $acr	; PGD for the current process
	addi	$r0.d, $acr, $acr
	move	$s3, $r0	; rw_mm_cause
	move.d  [$acr], $acr	; Get PMD
	beq	8f
	; Look up PTE in PMD
	lsrq	PAGE_SHIFT, $r0
	and.w	PAGE_MASK, $acr	; Remove PMD flags
	and.d	0x7ff, $r0	; Get PTE index into PMD (bit 13-23)
	addi    $r0.d, $acr, $acr
	move.d  [$acr], $acr	; Get PTE
	beq	9f
	movem	[$sp], $r2	; Restore r0-r2 in delay slot
	addq	12, $sp
	; Store in TLB
	move	$acr, $s5
4:	; Return
	move.d	[$sp+], $acr
	move	[$sp], $srs
	addq	4, $sp
	rete
	rfe

5:      ; last_refill_cause is invalid
	moveq	1, $r2
        addq    4, $r1          ; Point to last_refill_cause
	move.d	$r2, [$acr]	; refill_count = 1
	move	$s3, $r0	; Get rw_mm_cause
        ba      3b		; Continue normal processing
	move.d	$r0,[$r1]	; last_refill_cause = rw_mm_cause

6:      ; rw_mm_cause == last_refill_cause
        move.d  [$acr], $r2     ; Get refill_count
	cmpq	4, $r2		; refill_count > 4 ?
	bhi	7f		;   yes
	addq	1, $r2	        ; refill_count++
	ba	3b		; Continue normal processing
	move.d	$r2, [$acr]

7:	; refill_count > 4, error
	move.d	$acr, $r0       ; Save pointer to refill_count
	clear.d	[$r0]		; refill_count = 0

	;; rewind the short stack
	movem	[$sp], $r2	; Restore r0-r2
	addq	12, $sp
	move.d	[$sp+], $acr
	move	[$sp], $srs
	addq	4, $sp
	;; Keep it simple (slow), save all the regs.
	SAVE_ALL
	jsr	__flush_tlb_all
	nop
	ba	ret_from_intr	; Return
	nop

8:	; PMD missing, let the mm subsystem fix it up.
	movem	[$sp], $r2	; Restore r0-r2
9:      ; PTE missing, let the mm subsystem fix it up.
	addq	12, $sp
	move.d	[$sp+], $acr
	move	[$sp], $srs
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
