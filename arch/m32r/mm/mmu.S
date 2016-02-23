/*
 *  linux/arch/m32r/mm/mmu.S
 *
 *  Copyright (C) 2001 by Hiroyuki Kondo
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/smp.h>

	.text
#ifdef CONFIG_MMU

#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/m32r.h>

/*
 * TLB Miss Exception handler
 */
	.balign	16
ENTRY(tme_handler)
	.global	tlb_entry_i_dat
	.global	tlb_entry_d_dat

	SWITCH_TO_KERNEL_STACK

#if defined(CONFIG_ISA_M32R2)
	st	r0, @-sp
	st	r1, @-sp
	st	r2, @-sp
	st	r3, @-sp

	seth	r3, #high(MMU_REG_BASE)
	ld	r1, @(MESTS_offset, r3)	; r1: status     (MESTS reg.)
	ld	r0, @(MDEVP_offset, r3)	; r0: PFN + ASID (MDEVP reg.)
	st	r1, @(MESTS_offset, r3)	; clear status   (MESTS reg.)
	and3	r1, r1, #(MESTS_IT)
	bnez	r1, 1f			; instruction TLB miss?

;; data TLB miss
;;  input
;;   r0: PFN + ASID (MDEVP reg.)
;;   r1 - r3: free
;;  output
;;   r0: PFN + ASID
;;   r1: TLB entry base address
;;   r2: &tlb_entry_{i|d}_dat
;;   r3: free

#ifndef CONFIG_SMP
	seth	r2, #high(tlb_entry_d_dat)
	or3	r2, r2, #low(tlb_entry_d_dat)
#else	/* CONFIG_SMP */
	ldi	r1, #-8192
	seth	r2, #high(tlb_entry_d_dat)
	or3	r2, r2, #low(tlb_entry_d_dat)
	and	r1, sp
	ld	r1, @(16, r1)		; current_thread_info->cpu
	slli	r1, #2
	add	r2, r1
#endif	/* !CONFIG_SMP */
	seth	r1, #high(DTLB_BASE)
	or3	r1, r1, #low(DTLB_BASE)
	bra	2f

	.balign	16
	.fillinsn
1:
;; instrucntion TLB miss
;;  input
;;   r0: MDEVP reg. (included ASID)
;;   r1 - r3: free
;;  output
;;   r0: PFN + ASID
;;   r1: TLB entry base address
;;   r2: &tlb_entry_{i|d}_dat
;;   r3: free
	ldi	r3, #-4096
	and3	r0, r0, #(MMU_CONTEXT_ASID_MASK)
	mvfc	r1, bpc
	and	r1, r3
	or	r0, r1			; r0: PFN + ASID
#ifndef CONFIG_SMP
	seth	r2, #high(tlb_entry_i_dat)
	or3	r2, r2, #low(tlb_entry_i_dat)
#else	/* CONFIG_SMP */
	ldi	r1, #-8192
	seth	r2, #high(tlb_entry_i_dat)
	or3	r2, r2, #low(tlb_entry_i_dat)
	and	r1, sp
	ld	r1, @(16, r1)		; current_thread_info->cpu
	slli	r1, #2
	add	r2, r1
#endif	/* !CONFIG_SMP */
	seth	r1, #high(ITLB_BASE)
	or3	r1, r1, #low(ITLB_BASE)

	.fillinsn
2:
;; select TLB entry
;;  input
;;   r0: PFN + ASID
;;   r1: TLB entry base address
;;   r2: &tlb_entry_{i|d}_dat
;;   r3: free
;;  output
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2, r3: free
#ifdef CONFIG_ISA_DUAL_ISSUE
	ld	r3, @r2		||	srli	r1, #3
#else
	ld	r3, @r2
	srli	r1, #3
#endif
	add	r1, r3
	; tlb_entry_{d|i}_dat++;
	addi	r3, #1
	and3	r3, r3, #(NR_TLB_ENTRIES - 1)
#ifdef CONFIG_ISA_DUAL_ISSUE
	st	r3, @r2		||	slli	r1, #3
#else
	st	r3, @r2
	slli	r1, #3
#endif

;; load pte
;;  input
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2, r3: free
;;  output
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2: pte_data
;;   r3: free
	; pgd = *(unsigned long *)MPTB;
	ld24	r2, #(-MPTB - 1)
	srl3	r3, r0, #22
#ifdef CONFIG_ISA_DUAL_ISSUE
	not	r2, r2		    ||	slli	r3, #2	; r3: pgd offset
#else
	not	r2, r2
	slli	r3, #2
#endif
	ld	r2, @r2			; r2: pgd base addr (MPTB reg.)
	or	r3, r2			; r3: pmd addr

	; pmd = pmd_offset(pgd, address);
	ld	r3, @r3			; r3: pmd data
	beqz	r3, 3f			; pmd_none(*pmd) ?

	and3	r2, r3, #0xfff
	add3	r2, r2, #-355		; _KERNPG_TABLE(=0x163)
	bnez	r2, 3f			; pmd_bad(*pmd) ?
	ldi	r2, #-4096

	; pte = pte_offset(pmd, address);
	and	r2, r3			; r2: pte base addr
	srl3	r3, r0, #10
	and3	r3, r3, #0xffc		; r3: pte offset
	or	r3, r2
	seth	r2, #0x8000
	or	r3, r2			; r3: pte addr

	; pte_data = (unsigned long)pte_val(*pte);
	ld	r2, @r3			; r2: pte data
	and3	r3, r2, #2		; _PAGE_PRESENT(=2) check
	beqz	r3, 3f

	.fillinsn
5:
;; set tlb
;;  input
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2: pte_data
;;   r3: free
	st	r0, @r1			; set_tlb_tag(entry++, address);
	st	r2, @+r1		; set_tlb_data(entry, pte_data);

	.fillinsn
6:
	ld	r3, @sp+
	ld	r2, @sp+
	ld	r1, @sp+
	ld	r0, @sp+
	rte

	.fillinsn
3:
;; error
;;  input
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2, r3: free
;;  output
;;   r0: PFN + ASID
;;   r1: TLB entry address
;;   r2: pte_data
;;   r3: free
#ifdef CONFIG_ISA_DUAL_ISSUE
	bra	5b		    ||	ldi	r2, #2
#else
	ldi	r2, #2		; r2: pte_data = 0 | _PAGE_PRESENT(=2)
	bra	5b
#endif

#elif defined (CONFIG_ISA_M32R)

	st	sp, @-sp
	st	r0, @-sp
	st	r1, @-sp
	st	r2, @-sp
	st	r3, @-sp
	st	r4, @-sp

	seth	r3, #high(MMU_REG_BASE)
	ld	r0, @(MDEVA_offset,r3)	; r0: address  (MDEVA reg.)
	mvfc	r2, bpc			; r2: bpc
	ld	r1, @(MESTS_offset,r3)	; r1: status   (MESTS reg.)
	st	r1, @(MESTS_offset,r3)	; clear status (MESTS reg.)
	and3	r1, r1, #(MESTS_IT)
	beqz	r1, 1f			; data TLB miss?

;; instrucntion TLB miss
	mv	r0, r2			; address = bpc;
	; entry = (unsigned long *)ITLB_BASE+tlb_entry_i*2;
	seth	r3, #shigh(tlb_entry_i_dat)
	ld	r4, @(low(tlb_entry_i_dat),r3)
	sll3	r2, r4, #3
	seth	r1, #high(ITLB_BASE)
	or3	r1, r1, #low(ITLB_BASE)
	add	r2, r1			; r2: entry
	addi	r4, #1			; tlb_entry_i++;
	and3	r4, r4, #(NR_TLB_ENTRIES-1)
	st	r4, @(low(tlb_entry_i_dat),r3)
	bra	2f
	.fillinsn
1:
;; data TLB miss
	; entry = (unsigned long *)DTLB_BASE+tlb_entry_d*2;
	seth	r3, #shigh(tlb_entry_d_dat)
	ld	r4, @(low(tlb_entry_d_dat),r3)
	sll3	r2, r4, #3
	seth	r1, #high(DTLB_BASE)
	or3	r1, r1, #low(DTLB_BASE)
	add	r2, r1			; r2: entry
	addi	r4, #1			; tlb_entry_d++;
	and3	r4, r4, #(NR_TLB_ENTRIES-1)
	st	r4, @(low(tlb_entry_d_dat),r3)
	.fillinsn
2:
;; load pte
; r0: address, r2: entry
; r1,r3,r4: (free)
	; pgd = *(unsigned long *)MPTB;
	ld24	r1, #(-MPTB-1)
	not	r1, r1
	ld	r1, @r1
	srl3	r4, r0, #22
	sll3	r3, r4, #2
	add	r3, r1			; r3: pgd
	; pmd = pmd_offset(pgd, address);
	ld	r1, @r3			; r1: pmd
	beqz	r1, 3f			; pmd_none(*pmd) ?
;
	and3	r1, r1, #0x3ff
	ldi	r4, #0x163		; _KERNPG_TABLE(=0x163)
	bne	r1, r4, 3f		; pmd_bad(*pmd) ?

	.fillinsn
4:
	; pte = pte_offset(pmd, address);
	ld	r4, @r3			; r4: pte
	ldi	r3, #-4096
	and	r4, r3
	srl3	r3, r0, #10
	and3	r3, r3, #0xffc
	add	r4, r3
	seth	r3, #0x8000
	add	r4, r3			; r4: pte
	; pte_data = (unsigned long)pte_val(*pte);
	ld	r1, @r4			; r1: pte_data
	and3	r3, r1, #2		; _PAGE_PRESENT(=2) check
	beqz	r3, 3f

	.fillinsn
;; set tlb
; r0: address, r1: pte_data, r2: entry
; r3,r4: (free)
5:
	ldi	r3, #-4096		; set_tlb_tag(entry++, address);
	and	r3, r0
	seth	r4, #shigh(MASID)
	ld	r4, @(low(MASID),r4)	; r4: MASID
	and3	r4, r4, #(MMU_CONTEXT_ASID_MASK)
	or	r3, r4
	st	r3, @r2
	st	r1, @(4,r2)		; set_tlb_data(entry, pte_data);

	ld	r4, @sp+
	ld	r3, @sp+
	ld	r2, @sp+
	ld	r1, @sp+
	ld	r0, @sp+
	ld	sp, @sp+
	rte

	.fillinsn
3:
	ldi	r1, #2			; r1: pte_data = 0 | _PAGE_PRESENT(=2)
	bra	5b

#else
#error unknown isa configuration
#endif

ENTRY(init_tlb)
;; Set MMU Register
	seth	r0, #high(MMU_REG_BASE)	 ; Set MMU_REG_BASE higher
	or3	r0, r0, #low(MMU_REG_BASE)  ; Set MMU_REG_BASE lower
	ldi	r1, #0
	st	r1, @(MPSZ_offset,r0)	; Set MPSZ Reg(Page size 4KB:0 16KB:1 64KB:2)
	ldi	r1, #0
	st	r1, @(MASID_offset,r0)	; Set ASID Zero

;; Set TLB
	seth	r0, #high(ITLB_BASE)	; Set ITLB_BASE higher
	or3	r0, r0, #low(ITLB_BASE)	; Set ITLB_BASE lower
	seth	r1, #high(DTLB_BASE)	; Set DTLB_BASE higher
	or3	r1, r1, #low(DTLB_BASE)	; Set DTLB_BASE lower
	ldi	r2, #0
	ldi	r3, #NR_TLB_ENTRIES
	addi	r0, #-4
	addi	r1, #-4
clear_tlb:
	st	r2, @+r0		; VPA <- 0
	st	r2, @+r0		; PPA <- 0
	st	r2, @+r1		; VPA <- 0
	st	r2, @+r1		; PPA <- 0
	addi	r3, #-1
	bnez	r3, clear_tlb
;;
	jmp	r14

ENTRY(m32r_itlb_entrys)
ENTRY(m32r_otlb_entrys)

#endif  /* CONFIG_MMU */

	.end
