/*
 * TLB Exception Handling for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: April 2011 :
 *  -MMU v1: moved out legacy code into a seperate file
 *  -MMU v3: PD{0,1} bits layout changed: They don't overlap anymore,
 *      helps avoid a shift when preparing PD0 from PTE
 *
 * Vineetg: July 2009
 *  -For MMU V2, we need not do heuristics at the time of commiting a D-TLB
 *   entry, so that it doesn't knock out it's I-TLB entry
 *  -Some more fine tuning:
 *   bmsk instead of add, asl.cc instead of branch, delay slot utilise etc
 *
 * Vineetg: July 2009
 *  -Practically rewrote the I/D TLB Miss handlers
 *   Now 40 and 135 instructions a peice as compared to 131 and 449 resp.
 *   Hence Leaner by 1.5 K
 *   Used Conditional arithmetic to replace excessive branching
 *   Also used short instructions wherever possible
 *
 * Vineetg: Aug 13th 2008
 *  -Passing ECR (Exception Cause REG) to do_page_fault( ) for printing
 *   more information in case of a Fatality
 *
 * Vineetg: March 25th Bug #92690
 *  -Added Debug Code to check if sw-ASID == hw-ASID

 * Rahul Trivedi, Amit Bhor: Codito Technologies 2004
 */

	.cpu A7

#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/arcregs.h>
#include <asm/cache.h>
#include <asm/processor.h>
#if (CONFIG_ARC_MMU_VER == 1)
#include <asm/tlb-mmu1.h>
#endif

;--------------------------------------------------------------------------
; scratch memory to save the registers (r0-r3) used to code TLB refill Handler
; For details refer to comments before TLBMISS_FREEUP_REGS below
;--------------------------------------------------------------------------

ARCFP_DATA ex_saved_reg1
	.align 1 << L1_CACHE_SHIFT	; IMP: Must be Cache Line aligned
	.type   ex_saved_reg1, @object
#ifdef CONFIG_SMP
	.size   ex_saved_reg1, (CONFIG_NR_CPUS << L1_CACHE_SHIFT)
ex_saved_reg1:
	.zero (CONFIG_NR_CPUS << L1_CACHE_SHIFT)
#else
	.size   ex_saved_reg1, 16
ex_saved_reg1:
	.zero 16
#endif

;============================================================================
;  Troubleshooting Stuff
;============================================================================

; Linux keeps ASID (Address Space ID) in task->active_mm->context.asid
; When Creating TLB Entries, instead of doing 3 dependent loads from memory,
; we use the MMU PID Reg to get current ASID.
; In bizzare scenrios SW and HW ASID can get out-of-sync which is trouble.
; So we try to detect this in TLB Mis shandler


.macro DBG_ASID_MISMATCH

#ifdef CONFIG_ARC_DBG_TLB_PARANOIA

	; make sure h/w ASID is same as s/w ASID

	GET_CURR_TASK_ON_CPU  r3
	ld r0, [r3, TASK_ACT_MM]
	ld r0, [r0, MM_CTXT+MM_CTXT_ASID]

	lr r1, [ARC_REG_PID]
	and r1, r1, 0xFF
	breq r1, r0, 5f

	; Error if H/w and S/w ASID don't match, but NOT if in kernel mode
	lr  r0, [erstatus]
	bbit0 r0, STATUS_U_BIT, 5f

	; We sure are in troubled waters, Flag the error, but to do so
	; need to switch to kernel mode stack to call error routine
	GET_TSK_STACK_BASE   r3, sp

	; Call printk to shoutout aloud
	mov r0, 1
	j print_asid_mismatch

5:   ; ASIDs match so proceed normally
	nop

#endif

.endm

;============================================================================
;TLB Miss handling Code
;============================================================================

;-----------------------------------------------------------------------------
; This macro does the page-table lookup for the faulting address.
; OUT: r0 = PTE faulted on, r1 = ptr to PTE, r2 = Faulting V-address
.macro LOAD_FAULT_PTE

	lr  r2, [efa]

#ifndef CONFIG_SMP
	lr  r1, [ARC_REG_SCRATCH_DATA0] ; current pgd
#else
	GET_CURR_TASK_ON_CPU  r1
	ld  r1, [r1, TASK_ACT_MM]
	ld  r1, [r1, MM_PGD]
#endif

	lsr     r0, r2, PGDIR_SHIFT     ; Bits for indexing into PGD
	ld.as   r1, [r1, r0]            ; PGD entry corresp to faulting addr
	and.f   r1, r1, PAGE_MASK       ; Ignoring protection and other flags
	;   contains Ptr to Page Table
	bz.d    do_slow_path_pf         ; if no Page Table, do page fault

	; Get the PTE entry: The idea is
	; (1) x = addr >> PAGE_SHIFT 	-> masks page-off bits from @fault-addr
	; (2) y = x & (PTRS_PER_PTE - 1) -> to get index
	; (3) z = pgtbl[y]
	; To avoid the multiply by in end, we do the -2, <<2 below

	lsr     r0, r2, (PAGE_SHIFT - 2)
	and     r0, r0, ( (PTRS_PER_PTE - 1) << 2)
	ld.aw   r0, [r1, r0]            ; get PTE and PTE ptr for fault addr
#ifdef CONFIG_ARC_DBG_TLB_MISS_COUNT
	and.f 0, r0, _PAGE_PRESENT
	bz   1f
	ld   r2, [num_pte_not_present]
	add  r2, r2, 1
	st   r2, [num_pte_not_present]
1:
#endif

.endm

;-----------------------------------------------------------------
; Convert Linux PTE entry into TLB entry
; A one-word PTE entry is programmed as two-word TLB Entry [PD0:PD1] in mmu
; IN: r0 = PTE, r1 = ptr to PTE

.macro CONV_PTE_TO_TLB
	and r3, r0, PTE_BITS_IN_PD1 ; Extract permission flags+PFN from PTE
	sr  r3, [ARC_REG_TLBPD1]    ; these go in PD1

	and r2, r0, PTE_BITS_IN_PD0 ; Extract other PTE flags: (V)alid, (G)lb
#if (CONFIG_ARC_MMU_VER <= 2)   /* Neednot be done with v3 onwards */
	lsr r2, r2                  ; shift PTE flags to match layout in PD0
#endif

	lr  r3,[ARC_REG_TLBPD0]     ; MMU prepares PD0 with vaddr and asid

	or  r3, r3, r2              ; S | vaddr | {sasid|asid}
	sr  r3,[ARC_REG_TLBPD0]     ; rewrite PD0
.endm

;-----------------------------------------------------------------
; Commit the TLB entry into MMU

.macro COMMIT_ENTRY_TO_MMU

	/* Get free TLB slot: Set = computed from vaddr, way = random */
	sr  TLBGetIndex, [ARC_REG_TLBCOMMAND]

	/* Commit the Write */
#if (CONFIG_ARC_MMU_VER >= 2)   /* introduced in v2 */
	sr TLBWriteNI, [ARC_REG_TLBCOMMAND]
#else
	sr TLBWrite, [ARC_REG_TLBCOMMAND]
#endif
.endm

;-----------------------------------------------------------------
; ARC700 Exception Handling doesn't auto-switch stack and it only provides
; ONE scratch AUX reg "ARC_REG_SCRATCH_DATA0"
;
; For Non-SMP, the scratch AUX reg is repurposed to cache task PGD, so a
; "global" is used to free-up FIRST core reg to be able to code the rest of
; exception prologue (IRQ auto-disabled on Exceptions, so it's IRQ-safe).
; Since the Fast Path TLB Miss handler is coded with 4 regs, the remaining 3
; need to be saved as well by extending the "global" to be 4 words. Hence
;	".size   ex_saved_reg1, 16"
; [All of this dance is to avoid stack switching for each TLB Miss, since we
; only need to save only a handful of regs, as opposed to complete reg file]
;
; For ARC700 SMP, the "global" obviously can't be used for free up the FIRST
; core reg as it will not be SMP safe.
; Thus scratch AUX reg is used (and no longer used to cache task PGD).
; To save the rest of 3 regs - per cpu, the global is made "per-cpu".
; Epilogue thus has to locate the "per-cpu" storage for regs.
; To avoid cache line bouncing the per-cpu global is aligned/sized per
; L1_CACHE_SHIFT, despite fundamentally needing to be 12 bytes only. Hence
;	".size   ex_saved_reg1, (CONFIG_NR_CPUS << L1_CACHE_SHIFT)"

; As simple as that....

.macro TLBMISS_FREEUP_REGS
#ifdef CONFIG_SMP
	sr  r0, [ARC_REG_SCRATCH_DATA0]	; freeup r0 to code with
	GET_CPU_ID  r0			; get to per cpu scratch mem,
	lsl r0, r0, L1_CACHE_SHIFT	; cache line wide per cpu
	add r0, @ex_saved_reg1, r0
#else
	st    r0, [@ex_saved_reg1]
	mov_s r0, @ex_saved_reg1
#endif
	st_s  r1, [r0, 4]
	st_s  r2, [r0, 8]
	st_s  r3, [r0, 12]

	; VERIFY if the ASID in MMU-PID Reg is same as
	; one in Linux data structures

	DBG_ASID_MISMATCH
.endm

;-----------------------------------------------------------------
.macro TLBMISS_RESTORE_REGS
#ifdef CONFIG_SMP
	GET_CPU_ID  r0			; get to per cpu scratch mem
	lsl r0, r0, L1_CACHE_SHIFT	; each is cache line wide
	add r0, @ex_saved_reg1, r0
	ld_s  r3, [r0,12]
	ld_s  r2, [r0, 8]
	ld_s  r1, [r0, 4]
	lr    r0, [ARC_REG_SCRATCH_DATA0]
#else
	mov_s r0, @ex_saved_reg1
	ld_s  r3, [r0,12]
	ld_s  r2, [r0, 8]
	ld_s  r1, [r0, 4]
	ld_s  r0, [r0]
#endif
.endm

ARCFP_CODE	;Fast Path Code, candidate for ICCM

;-----------------------------------------------------------------------------
; I-TLB Miss Exception Handler
;-----------------------------------------------------------------------------

ARC_ENTRY EV_TLBMissI

	TLBMISS_FREEUP_REGS

#ifdef CONFIG_ARC_DBG_TLB_MISS_COUNT
	ld  r0, [@numitlb]
	add r0, r0, 1
	st  r0, [@numitlb]
#endif

	;----------------------------------------------------------------
	; Get the PTE corresponding to V-addr accessed
	LOAD_FAULT_PTE

	;----------------------------------------------------------------
	; VERIFY_PTE: Check if PTE permissions approp for executing code
	cmp_s   r2, VMALLOC_START
	mov.lo  r2, (_PAGE_PRESENT | _PAGE_U_READ | _PAGE_U_EXECUTE)
	mov.hs  r2, (_PAGE_PRESENT | _PAGE_K_READ | _PAGE_K_EXECUTE)

	and     r3, r0, r2  ; Mask out NON Flag bits from PTE
	xor.f   r3, r3, r2  ; check ( ( pte & flags_test ) == flags_test )
	bnz     do_slow_path_pf

	; Let Linux VM know that the page was accessed
	or      r0, r0, (_PAGE_PRESENT | _PAGE_ACCESSED)  ; set Accessed Bit
	st_s    r0, [r1]                                  ; Write back PTE

	CONV_PTE_TO_TLB
	COMMIT_ENTRY_TO_MMU
	TLBMISS_RESTORE_REGS
	rtie

ARC_EXIT EV_TLBMissI

;-----------------------------------------------------------------------------
; D-TLB Miss Exception Handler
;-----------------------------------------------------------------------------

ARC_ENTRY EV_TLBMissD

	TLBMISS_FREEUP_REGS

#ifdef CONFIG_ARC_DBG_TLB_MISS_COUNT
	ld  r0, [@numdtlb]
	add r0, r0, 1
	st  r0, [@numdtlb]
#endif

	;----------------------------------------------------------------
	; Get the PTE corresponding to V-addr accessed
	; If PTE exists, it will setup, r0 = PTE, r1 = Ptr to PTE
	LOAD_FAULT_PTE

	;----------------------------------------------------------------
	; VERIFY_PTE: Chk if PTE permissions approp for data access (R/W/R+W)

	mov_s   r2, 0
	lr      r3, [ecr]
	btst_s  r3, ECR_C_BIT_DTLB_LD_MISS	; Read Access
	or.nz   r2, r2, _PAGE_U_READ      	; chk for Read flag in PTE
	btst_s  r3, ECR_C_BIT_DTLB_ST_MISS	; Write Access
	or.nz   r2, r2, _PAGE_U_WRITE     	; chk for Write flag in PTE
	; Above laddering takes care of XCHG access
	;   which is both Read and Write

	; If kernel mode access, ; make _PAGE_xx flags as _PAGE_K_xx
	; For copy_(to|from)_user, despite exception taken in kernel mode,
	; this code is not hit, because EFA would still be the user mode
	; address (EFA < 0x6000_0000).
	; This code is for legit kernel mode faults, vmalloc specifically
	; (EFA: 0x7000_0000 to 0x7FFF_FFFF)

	lr      r3, [efa]
	cmp     r3, VMALLOC_START - 1   ; If kernel mode access
	asl.hi  r2, r2, 3               ; make _PAGE_xx flags as _PAGE_K_xx
	or      r2, r2, _PAGE_PRESENT   ; Common flag for K/U mode

	; By now, r2 setup with all the Flags we need to check in PTE
	and     r3, r0, r2              ; Mask out NON Flag bits from PTE
	brne.d  r3, r2, do_slow_path_pf ; is ((pte & flags_test) == flags_test)

	;----------------------------------------------------------------
	; UPDATE_PTE: Let Linux VM know that page was accessed/dirty
	lr      r3, [ecr]
	or      r0, r0, (_PAGE_PRESENT | _PAGE_ACCESSED) ; Accessed bit always
	btst_s  r3,  ECR_C_BIT_DTLB_ST_MISS   ; See if it was a Write Access ?
	or.nz   r0, r0, _PAGE_MODIFIED        ; if Write, set Dirty bit as well
	st_s    r0, [r1]                      ; Write back PTE

	CONV_PTE_TO_TLB

#if (CONFIG_ARC_MMU_VER == 1)
	; MMU with 2 way set assoc J-TLB, needs some help in pathetic case of
	; memcpy where 3 parties contend for 2 ways, ensuing a livelock.
	; But only for old MMU or one with Metal Fix
	TLB_WRITE_HEURISTICS
#endif

	COMMIT_ENTRY_TO_MMU
	TLBMISS_RESTORE_REGS
	rtie

;-------- Common routine to call Linux Page Fault Handler -----------
do_slow_path_pf:

	; Restore the 4-scratch regs saved by fast path miss handler
	TLBMISS_RESTORE_REGS

	; Slow path TLB Miss handled as a regular ARC Exception
	; (stack switching / save the complete reg-file).
	; That requires freeing up r9
	EXCPN_PROLOG_FREEUP_REG r9

	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	; ------- setup args for Linux Page fault Hanlder ---------
	mov_s r0, sp
	lr  r1, [efa]
	lr  r2, [ecr]

	; We don't want exceptions to be disabled while the fault is handled.
	; Now that we have saved the context we return from exception hence
	; exceptions get re-enable

	FAKE_RET_FROM_EXCPN  r9

	bl  do_page_fault
	b   ret_from_exception

ARC_EXIT EV_TLBMissD

ARC_ENTRY EV_TLBMissB   ; Bogus entry to measure sz of DTLBMiss hdlr
