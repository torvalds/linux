/*
 * arch/ia64/kernel/ivt.S
 *
 * Copyright (C) 1998-2001, 2003, 2005 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 2000, 2002-2003 Intel Co
 *	Asit Mallick <asit.k.mallick@intel.com>
 *      Suresh Siddha <suresh.b.siddha@intel.com>
 *      Kenneth Chen <kenneth.w.chen@intel.com>
 *      Fenghua Yu <fenghua.yu@intel.com>
 *
 * 00/08/23 Asit Mallick <asit.k.mallick@intel.com> TLB handling for SMP
 * 00/12/20 David Mosberger-Tang <davidm@hpl.hp.com> DTLB/ITLB handler now uses virtual PT.
 *
 * Copyright (C) 2005 Hewlett-Packard Co
 *	Dan Magenheimer <dan.magenheimer@hp.com>
 *      Xen paravirtualization
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *                    pv_ops.
 *      Yaozu (Eddie) Dong <eddie.dong@intel.com>
 */
/*
 * This file defines the interruption vector table used by the CPU.
 * It does not include one entry per possible cause of interruption.
 *
 * The first 20 entries of the table contain 64 bundles each while the
 * remaining 48 entries contain only 16 bundles each.
 *
 * The 64 bundles are used to allow inlining the whole handler for critical
 * interruptions like TLB misses.
 *
 *  For each entry, the comment is as follows:
 *
 *		// 0x1c00 Entry 7 (size 64 bundles) Data Key Miss (12,51)
 *  entry offset ----/     /         /                  /          /
 *  entry number ---------/         /                  /          /
 *  size of the entry -------------/                  /          /
 *  vector name -------------------------------------/          /
 *  interruptions triggering this vector ----------------------/
 *
 * The table is 32KB in size and must be aligned on 32KB boundary.
 * (The CPU ignores the 15 lower bits of the address)
 *
 * Table is based upon EAS2.6 (Oct 1999)
 */


#include <asm/asmmacro.h>
#include <asm/break.h>
#include <asm/kregs.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/errno.h>

#if 1
# define PSR_DEFAULT_BITS	psr.ac
#else
# define PSR_DEFAULT_BITS	0
#endif

#if 0
  /*
   * This lets you track the last eight faults that occurred on the CPU.  Make sure ar.k2 isn't
   * needed for something else before enabling this...
   */
# define DBG_FAULT(i)	mov r16=ar.k2;;	shl r16=r16,8;;	add r16=(i),r16;;mov ar.k2=r16
#else
# define DBG_FAULT(i)
#endif

#include "minstate.h"

#define FAULT(n)									\
	mov r31=pr;									\
	mov r19=n;;			/* prepare to save predicates */		\
	br.sptk.many dispatch_to_fault_handler

	.section .text..ivt,"ax"

	.align 32768	// align on 32KB boundary
	.global ia64_ivt
ia64_ivt:
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0000 Entry 0 (size 64 bundles) VHPT Translation (8,20,47)
ENTRY(vhpt_miss)
	DBG_FAULT(0)
	/*
	 * The VHPT vector is invoked when the TLB entry for the virtual page table
	 * is missing.  This happens only as a result of a previous
	 * (the "original") TLB miss, which may either be caused by an instruction
	 * fetch or a data access (or non-access).
	 *
	 * What we do here is normal TLB miss handing for the _original_ miss,
	 * followed by inserting the TLB entry for the virtual page table page
	 * that the VHPT walker was attempting to access.  The latter gets
	 * inserted as long as page table entry above pte level have valid
	 * mappings for the faulting address.  The TLB entry for the original
	 * miss gets inserted only if the pte entry indicates that the page is
	 * present.
	 *
	 * do_page_fault gets invoked in the following cases:
	 *	- the faulting virtual address uses unimplemented address bits
	 *	- the faulting virtual address has no valid page table mapping
	 */
	MOV_FROM_IFA(r16)			// get address that caused the TLB miss
#ifdef CONFIG_HUGETLB_PAGE
	movl r18=PAGE_SHIFT
	MOV_FROM_ITIR(r25)
#endif
	;;
	RSM_PSR_DT				// use physical addressing for data
	mov r31=pr				// save the predicate registers
	mov r19=IA64_KR(PT_BASE)		// get page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	shr.u r17=r16,61			// get the region number into r17
	;;
	shr.u r22=r21,3
#ifdef CONFIG_HUGETLB_PAGE
	extr.u r26=r25,2,6
	;;
	cmp.ne p8,p0=r18,r26
	sub r27=r26,r18
	;;
(p8)	dep r25=r18,r25,2,6
(p8)	shr r22=r22,r27
#endif
	;;
	cmp.eq p6,p7=5,r17			// is IFA pointing into to region 5?
	shr.u r18=r22,PGDIR_SHIFT		// get bottom portion of pgd index bit
	;;
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place

	srlz.d
	LOAD_PHYSICAL(p6, r19, swapper_pg_dir)	// region 5 is rooted at swapper_pg_dir

	.pred.rel "mutex", p6, p7
(p6)	shr.u r21=r21,PGDIR_SHIFT+PAGE_SHIFT
(p7)	shr.u r21=r21,PGDIR_SHIFT+PAGE_SHIFT-3
	;;
(p6)	dep r17=r18,r19,3,(PAGE_SHIFT-3)	// r17=pgd_offset for region 5
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=pgd_offset for region[0-4]
	cmp.eq p7,p6=0,r21			// unused address bits all zeroes?
#ifdef CONFIG_PGTABLE_4
	shr.u r28=r22,PUD_SHIFT			// shift pud index into position
#else
	shr.u r18=r22,PMD_SHIFT			// shift pmd index into position
#endif
	;;
	ld8 r17=[r17]				// get *pgd (may be 0)
	;;
(p7)	cmp.eq p6,p7=r17,r0			// was pgd_present(*pgd) == NULL?
#ifdef CONFIG_PGTABLE_4
	dep r28=r28,r17,3,(PAGE_SHIFT-3)	// r28=pud_offset(pgd,addr)
	;;
	shr.u r18=r22,PMD_SHIFT			// shift pmd index into position
(p7)	ld8 r29=[r28]				// get *pud (may be 0)
	;;
(p7)	cmp.eq.or.andcm p6,p7=r29,r0		// was pud_present(*pud) == NULL?
	dep r17=r18,r29,3,(PAGE_SHIFT-3)	// r17=pmd_offset(pud,addr)
#else
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// r17=pmd_offset(pgd,addr)
#endif
	;;
(p7)	ld8 r20=[r17]				// get *pmd (may be 0)
	shr.u r19=r22,PAGE_SHIFT		// shift pte index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r20,r0		// was pmd_present(*pmd) == NULL?
	dep r21=r19,r20,3,(PAGE_SHIFT-3)	// r21=pte_offset(pmd,addr)
	;;
(p7)	ld8 r18=[r21]				// read *pte
	MOV_FROM_ISR(r19)			// cr.isr bit 32 tells us if this is an insn miss
	;;
(p7)	tbit.z p6,p7=r18,_PAGE_P_BIT		// page present bit cleared?
	MOV_FROM_IHA(r22)			// get the VHPT address that caused the TLB miss
	;;					// avoid RAW on p7
(p7)	tbit.nz.unc p10,p11=r19,32		// is it an instruction TLB miss?
	dep r23=0,r20,0,PAGE_SHIFT		// clear low bits to get page address
	;;
	ITC_I_AND_D(p10, p11, r18, r24)		// insert the instruction TLB entry and
						// insert the data TLB entry
(p6)	br.cond.spnt.many page_fault		// handle bad address/page not present (page fault)
	MOV_TO_IFA(r22, r24)

#ifdef CONFIG_HUGETLB_PAGE
	MOV_TO_ITIR(p8, r25, r24)		// change to default page-size for VHPT
#endif

	/*
	 * Now compute and insert the TLB entry for the virtual page table.  We never
	 * execute in a page table page so there is no need to set the exception deferral
	 * bit.
	 */
	adds r24=__DIRTY_BITS_NO_ED|_PAGE_PL_0|_PAGE_AR_RW,r23
	;;
	ITC_D(p7, r24, r25)
	;;
#ifdef CONFIG_SMP
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data

	/*
	 * Re-check pagetable entry.  If they changed, we may have received a ptc.g
	 * between reading the pagetable and the "itc".  If so, flush the entry we
	 * inserted and retry.  At this point, we have:
	 *
	 * r28 = equivalent of pud_offset(pgd, ifa)
	 * r17 = equivalent of pmd_offset(pud, ifa)
	 * r21 = equivalent of pte_offset(pmd, ifa)
	 *
	 * r29 = *pud
	 * r20 = *pmd
	 * r18 = *pte
	 */
	ld8 r25=[r21]				// read *pte again
	ld8 r26=[r17]				// read *pmd again
#ifdef CONFIG_PGTABLE_4
	ld8 r19=[r28]				// read *pud again
#endif
	cmp.ne p6,p7=r0,r0
	;;
	cmp.ne.or.andcm p6,p7=r26,r20		// did *pmd change
#ifdef CONFIG_PGTABLE_4
	cmp.ne.or.andcm p6,p7=r19,r29		// did *pud change
#endif
	mov r27=PAGE_SHIFT<<2
	;;
(p6)	ptc.l r22,r27				// purge PTE page translation
(p7)	cmp.ne.or.andcm p6,p7=r25,r18		// did *pte change
	;;
(p6)	ptc.l r16,r27				// purge translation
#endif

	mov pr=r31,-1				// restore predicate registers
	RFI
END(vhpt_miss)

	.org ia64_ivt+0x400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0400 Entry 1 (size 64 bundles) ITLB (21)
ENTRY(itlb_miss)
	DBG_FAULT(1)
	/*
	 * The ITLB handler accesses the PTE via the virtually mapped linear
	 * page table.  If a nested TLB miss occurs, we switch into physical
	 * mode, walk the page table, and then re-execute the PTE read and
	 * go on normally after that.
	 */
	MOV_FROM_IFA(r16)			// get virtual address
	mov r29=b0				// save b0
	mov r31=pr				// save predicates
.itlb_fault:
	MOV_FROM_IHA(r17)			// get virtual address of PTE
	movl r30=1f				// load nested fault continuation point
	;;
1:	ld8 r18=[r17]				// read *pte
	;;
	mov b0=r29
	tbit.z p6,p0=r18,_PAGE_P_BIT		// page present bit cleared?
(p6)	br.cond.spnt page_fault
	;;
	ITC_I(p0, r18, r19)
	;;
#ifdef CONFIG_SMP
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data

	ld8 r19=[r17]				// read *pte again and see if same
	mov r20=PAGE_SHIFT<<2			// setup page size for purge
	;;
	cmp.ne p7,p0=r18,r19
	;;
(p7)	ptc.l r16,r20
#endif
	mov pr=r31,-1
	RFI
END(itlb_miss)

	.org ia64_ivt+0x0800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0800 Entry 2 (size 64 bundles) DTLB (9,48)
ENTRY(dtlb_miss)
	DBG_FAULT(2)
	/*
	 * The DTLB handler accesses the PTE via the virtually mapped linear
	 * page table.  If a nested TLB miss occurs, we switch into physical
	 * mode, walk the page table, and then re-execute the PTE read and
	 * go on normally after that.
	 */
	MOV_FROM_IFA(r16)			// get virtual address
	mov r29=b0				// save b0
	mov r31=pr				// save predicates
dtlb_fault:
	MOV_FROM_IHA(r17)			// get virtual address of PTE
	movl r30=1f				// load nested fault continuation point
	;;
1:	ld8 r18=[r17]				// read *pte
	;;
	mov b0=r29
	tbit.z p6,p0=r18,_PAGE_P_BIT		// page present bit cleared?
(p6)	br.cond.spnt page_fault
	;;
	ITC_D(p0, r18, r19)
	;;
#ifdef CONFIG_SMP
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data

	ld8 r19=[r17]				// read *pte again and see if same
	mov r20=PAGE_SHIFT<<2			// setup page size for purge
	;;
	cmp.ne p7,p0=r18,r19
	;;
(p7)	ptc.l r16,r20
#endif
	mov pr=r31,-1
	RFI
END(dtlb_miss)

	.org ia64_ivt+0x0c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x0c00 Entry 3 (size 64 bundles) Alt ITLB (19)
ENTRY(alt_itlb_miss)
	DBG_FAULT(3)
	MOV_FROM_IFA(r16)	// get address that caused the TLB miss
	movl r17=PAGE_KERNEL
	MOV_FROM_IPSR(p0, r21)
	movl r19=(((1 << IA64_MAX_PHYS_BITS) - 1) & ~0xfff)
	mov r31=pr
	;;
#ifdef CONFIG_DISABLE_VHPT
	shr.u r22=r16,61			// get the region number into r21
	;;
	cmp.gt p8,p0=6,r22			// user mode
	;;
	THASH(p8, r17, r16, r23)
	;;
	MOV_TO_IHA(p8, r17, r23)
(p8)	mov r29=b0				// save b0
(p8)	br.cond.dptk .itlb_fault
#endif
	extr.u r23=r21,IA64_PSR_CPL0_BIT,2	// extract psr.cpl
	and r19=r19,r16		// clear ed, reserved bits, and PTE control bits
	shr.u r18=r16,57	// move address bit 61 to bit 4
	;;
	andcm r18=0x10,r18	// bit 4=~address-bit(61)
	cmp.ne p8,p0=r0,r23	// psr.cpl != 0?
	or r19=r17,r19		// insert PTE control bits into r19
	;;
	or r19=r19,r18		// set bit 4 (uncached) if the access was to region 6
(p8)	br.cond.spnt page_fault
	;;
	ITC_I(p0, r19, r18)	// insert the TLB entry
	mov pr=r31,-1
	RFI
END(alt_itlb_miss)

	.org ia64_ivt+0x1000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1000 Entry 4 (size 64 bundles) Alt DTLB (7,46)
ENTRY(alt_dtlb_miss)
	DBG_FAULT(4)
	MOV_FROM_IFA(r16)	// get address that caused the TLB miss
	movl r17=PAGE_KERNEL
	MOV_FROM_ISR(r20)
	movl r19=(((1 << IA64_MAX_PHYS_BITS) - 1) & ~0xfff)
	MOV_FROM_IPSR(p0, r21)
	mov r31=pr
	mov r24=PERCPU_ADDR
	;;
#ifdef CONFIG_DISABLE_VHPT
	shr.u r22=r16,61			// get the region number into r21
	;;
	cmp.gt p8,p0=6,r22			// access to region 0-5
	;;
	THASH(p8, r17, r16, r25)
	;;
	MOV_TO_IHA(p8, r17, r25)
(p8)	mov r29=b0				// save b0
(p8)	br.cond.dptk dtlb_fault
#endif
	cmp.ge p10,p11=r16,r24			// access to per_cpu_data?
	tbit.z p12,p0=r16,61			// access to region 6?
	mov r25=PERCPU_PAGE_SHIFT << 2
	mov r26=PERCPU_PAGE_SIZE
	nop.m 0
	nop.b 0
	;;
(p10)	mov r19=IA64_KR(PER_CPU_DATA)
(p11)	and r19=r19,r16				// clear non-ppn fields
	extr.u r23=r21,IA64_PSR_CPL0_BIT,2	// extract psr.cpl
	and r22=IA64_ISR_CODE_MASK,r20		// get the isr.code field
	tbit.nz p6,p7=r20,IA64_ISR_SP_BIT	// is speculation bit on?
	tbit.nz p9,p0=r20,IA64_ISR_NA_BIT	// is non-access bit on?
	;;
(p10)	sub r19=r19,r26
	MOV_TO_ITIR(p10, r25, r24)
	cmp.ne p8,p0=r0,r23
(p9)	cmp.eq.or.andcm p6,p7=IA64_ISR_CODE_LFETCH,r22	// check isr.code field
(p12)	dep r17=-1,r17,4,1			// set ma=UC for region 6 addr
(p8)	br.cond.spnt page_fault

	dep r21=-1,r21,IA64_PSR_ED_BIT,1
	;;
	or r19=r19,r17		// insert PTE control bits into r19
	MOV_TO_IPSR(p6, r21, r24)
	;;
	ITC_D(p7, r19, r18)	// insert the TLB entry
	mov pr=r31,-1
	RFI
END(alt_dtlb_miss)

	.org ia64_ivt+0x1400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1400 Entry 5 (size 64 bundles) Data nested TLB (6,45)
ENTRY(nested_dtlb_miss)
	/*
	 * In the absence of kernel bugs, we get here when the virtually mapped linear
	 * page table is accessed non-speculatively (e.g., in the Dirty-bit, Instruction
	 * Access-bit, or Data Access-bit faults).  If the DTLB entry for the virtual page
	 * table is missing, a nested TLB miss fault is triggered and control is
	 * transferred to this point.  When this happens, we lookup the pte for the
	 * faulting address by walking the page table in physical mode and return to the
	 * continuation point passed in register r30 (or call page_fault if the address is
	 * not mapped).
	 *
	 * Input:	r16:	faulting address
	 *		r29:	saved b0
	 *		r30:	continuation address
	 *		r31:	saved pr
	 *
	 * Output:	r17:	physical address of PTE of faulting address
	 *		r29:	saved b0
	 *		r30:	continuation address
	 *		r31:	saved pr
	 *
	 * Clobbered:	b0, r18, r19, r21, r22, psr.dt (cleared)
	 */
	RSM_PSR_DT				// switch to using physical data addressing
	mov r19=IA64_KR(PT_BASE)		// get the page table base address
	shl r21=r16,3				// shift bit 60 into sign bit
	MOV_FROM_ITIR(r18)
	;;
	shr.u r17=r16,61			// get the region number into r17
	extr.u r18=r18,2,6			// get the faulting page size
	;;
	cmp.eq p6,p7=5,r17			// is faulting address in region 5?
	add r22=-PAGE_SHIFT,r18			// adjustment for hugetlb address
	add r18=PGDIR_SHIFT-PAGE_SHIFT,r18
	;;
	shr.u r22=r16,r22
	shr.u r18=r16,r18
(p7)	dep r17=r17,r19,(PAGE_SHIFT-3),3	// put region number bits in place

	srlz.d
	LOAD_PHYSICAL(p6, r19, swapper_pg_dir)	// region 5 is rooted at swapper_pg_dir

	.pred.rel "mutex", p6, p7
(p6)	shr.u r21=r21,PGDIR_SHIFT+PAGE_SHIFT
(p7)	shr.u r21=r21,PGDIR_SHIFT+PAGE_SHIFT-3
	;;
(p6)	dep r17=r18,r19,3,(PAGE_SHIFT-3)	// r17=pgd_offset for region 5
(p7)	dep r17=r18,r17,3,(PAGE_SHIFT-6)	// r17=pgd_offset for region[0-4]
	cmp.eq p7,p6=0,r21			// unused address bits all zeroes?
#ifdef CONFIG_PGTABLE_4
	shr.u r18=r22,PUD_SHIFT			// shift pud index into position
#else
	shr.u r18=r22,PMD_SHIFT			// shift pmd index into position
#endif
	;;
	ld8 r17=[r17]				// get *pgd (may be 0)
	;;
(p7)	cmp.eq p6,p7=r17,r0			// was pgd_present(*pgd) == NULL?
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// r17=p[u|m]d_offset(pgd,addr)
	;;
#ifdef CONFIG_PGTABLE_4
(p7)	ld8 r17=[r17]				// get *pud (may be 0)
	shr.u r18=r22,PMD_SHIFT			// shift pmd index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r17,r0		// was pud_present(*pud) == NULL?
	dep r17=r18,r17,3,(PAGE_SHIFT-3)	// r17=pmd_offset(pud,addr)
	;;
#endif
(p7)	ld8 r17=[r17]				// get *pmd (may be 0)
	shr.u r19=r22,PAGE_SHIFT		// shift pte index into position
	;;
(p7)	cmp.eq.or.andcm p6,p7=r17,r0		// was pmd_present(*pmd) == NULL?
	dep r17=r19,r17,3,(PAGE_SHIFT-3)	// r17=pte_offset(pmd,addr);
(p6)	br.cond.spnt page_fault
	mov b0=r30
	br.sptk.many b0				// return to continuation point
END(nested_dtlb_miss)

	.org ia64_ivt+0x1800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1800 Entry 6 (size 64 bundles) Instruction Key Miss (24)
ENTRY(ikey_miss)
	DBG_FAULT(6)
	FAULT(6)
END(ikey_miss)

	.org ia64_ivt+0x1c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x1c00 Entry 7 (size 64 bundles) Data Key Miss (12,51)
ENTRY(dkey_miss)
	DBG_FAULT(7)
	FAULT(7)
END(dkey_miss)

	.org ia64_ivt+0x2000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2000 Entry 8 (size 64 bundles) Dirty-bit (54)
ENTRY(dirty_bit)
	DBG_FAULT(8)
	/*
	 * What we do here is to simply turn on the dirty bit in the PTE.  We need to
	 * update both the page-table and the TLB entry.  To efficiently access the PTE,
	 * we address it through the virtual page table.  Most likely, the TLB entry for
	 * the relevant virtual page table page is still present in the TLB so we can
	 * normally do this without additional TLB misses.  In case the necessary virtual
	 * page table TLB entry isn't present, we take a nested TLB miss hit where we look
	 * up the physical address of the L3 PTE and then continue at label 1 below.
	 */
	MOV_FROM_IFA(r16)			// get the address that caused the fault
	movl r30=1f				// load continuation point in case of nested fault
	;;
	THASH(p0, r17, r16, r18)		// compute virtual address of L3 PTE
	mov r29=b0				// save b0 in case of nested fault
	mov r31=pr				// save pr
#ifdef CONFIG_SMP
	mov r28=ar.ccv				// save ar.ccv
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	mov ar.ccv=r18				// set compare value for cmpxchg
	or r25=_PAGE_D|_PAGE_A,r18		// set the dirty and accessed bits
	tbit.z p7,p6 = r18,_PAGE_P_BIT		// Check present bit
	;;
(p6)	cmpxchg8.acq r26=[r17],r25,ar.ccv	// Only update if page is present
	mov r24=PAGE_SHIFT<<2
	;;
(p6)	cmp.eq p6,p7=r26,r18			// Only compare if page is present
	;;
	ITC_D(p6, r25, r18)			// install updated PTE
	;;
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data

	ld8 r18=[r17]				// read PTE again
	;;
	cmp.eq p6,p7=r18,r25			// is it same as the newly installed
	;;
(p7)	ptc.l r16,r24
	mov b0=r29				// restore b0
	mov ar.ccv=r28
#else
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	or r18=_PAGE_D|_PAGE_A,r18		// set the dirty and accessed bits
	mov b0=r29				// restore b0
	;;
	st8 [r17]=r18				// store back updated PTE
	ITC_D(p0, r18, r16)			// install updated PTE
#endif
	mov pr=r31,-1				// restore pr
	RFI
END(dirty_bit)

	.org ia64_ivt+0x2400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2400 Entry 9 (size 64 bundles) Instruction Access-bit (27)
ENTRY(iaccess_bit)
	DBG_FAULT(9)
	// Like Entry 8, except for instruction access
	MOV_FROM_IFA(r16)			// get the address that caused the fault
	movl r30=1f				// load continuation point in case of nested fault
	mov r31=pr				// save predicates
#ifdef CONFIG_ITANIUM
	/*
	 * Erratum 10 (IFA may contain incorrect address) has "NoFix" status.
	 */
	MOV_FROM_IPSR(p0, r17)
	;;
	MOV_FROM_IIP(r18)
	tbit.z p6,p0=r17,IA64_PSR_IS_BIT	// IA64 instruction set?
	;;
(p6)	mov r16=r18				// if so, use cr.iip instead of cr.ifa
#endif /* CONFIG_ITANIUM */
	;;
	THASH(p0, r17, r16, r18)		// compute virtual address of L3 PTE
	mov r29=b0				// save b0 in case of nested fault)
#ifdef CONFIG_SMP
	mov r28=ar.ccv				// save ar.ccv
	;;
1:	ld8 r18=[r17]
	;;
	mov ar.ccv=r18				// set compare value for cmpxchg
	or r25=_PAGE_A,r18			// set the accessed bit
	tbit.z p7,p6 = r18,_PAGE_P_BIT	 	// Check present bit
	;;
(p6)	cmpxchg8.acq r26=[r17],r25,ar.ccv	// Only if page present
	mov r24=PAGE_SHIFT<<2
	;;
(p6)	cmp.eq p6,p7=r26,r18			// Only if page present
	;;
	ITC_I(p6, r25, r26)			// install updated PTE
	;;
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data

	ld8 r18=[r17]				// read PTE again
	;;
	cmp.eq p6,p7=r18,r25			// is it same as the newly installed
	;;
(p7)	ptc.l r16,r24
	mov b0=r29				// restore b0
	mov ar.ccv=r28
#else /* !CONFIG_SMP */
	;;
1:	ld8 r18=[r17]
	;;
	or r18=_PAGE_A,r18			// set the accessed bit
	mov b0=r29				// restore b0
	;;
	st8 [r17]=r18				// store back updated PTE
	ITC_I(p0, r18, r16)			// install updated PTE
#endif /* !CONFIG_SMP */
	mov pr=r31,-1
	RFI
END(iaccess_bit)

	.org ia64_ivt+0x2800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2800 Entry 10 (size 64 bundles) Data Access-bit (15,55)
ENTRY(daccess_bit)
	DBG_FAULT(10)
	// Like Entry 8, except for data access
	MOV_FROM_IFA(r16)			// get the address that caused the fault
	movl r30=1f				// load continuation point in case of nested fault
	;;
	THASH(p0, r17, r16, r18)		// compute virtual address of L3 PTE
	mov r31=pr
	mov r29=b0				// save b0 in case of nested fault)
#ifdef CONFIG_SMP
	mov r28=ar.ccv				// save ar.ccv
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	mov ar.ccv=r18				// set compare value for cmpxchg
	or r25=_PAGE_A,r18			// set the dirty bit
	tbit.z p7,p6 = r18,_PAGE_P_BIT		// Check present bit
	;;
(p6)	cmpxchg8.acq r26=[r17],r25,ar.ccv	// Only if page is present
	mov r24=PAGE_SHIFT<<2
	;;
(p6)	cmp.eq p6,p7=r26,r18			// Only if page is present
	;;
	ITC_D(p6, r25, r26)			// install updated PTE
	/*
	 * Tell the assemblers dependency-violation checker that the above "itc" instructions
	 * cannot possibly affect the following loads:
	 */
	dv_serialize_data
	;;
	ld8 r18=[r17]				// read PTE again
	;;
	cmp.eq p6,p7=r18,r25			// is it same as the newly installed
	;;
(p7)	ptc.l r16,r24
	mov ar.ccv=r28
#else
	;;
1:	ld8 r18=[r17]
	;;					// avoid RAW on r18
	or r18=_PAGE_A,r18			// set the accessed bit
	;;
	st8 [r17]=r18				// store back updated PTE
	ITC_D(p0, r18, r16)			// install updated PTE
#endif
	mov b0=r29				// restore b0
	mov pr=r31,-1
	RFI
END(daccess_bit)

	.org ia64_ivt+0x2c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x2c00 Entry 11 (size 64 bundles) Break instruction (33)
ENTRY(break_fault)
	/*
	 * The streamlined system call entry/exit paths only save/restore the initial part
	 * of pt_regs.  This implies that the callers of system-calls must adhere to the
	 * normal procedure calling conventions.
	 *
	 *   Registers to be saved & restored:
	 *	CR registers: cr.ipsr, cr.iip, cr.ifs
	 *	AR registers: ar.unat, ar.pfs, ar.rsc, ar.rnat, ar.bspstore, ar.fpsr
	 * 	others: pr, b0, b6, loadrs, r1, r11, r12, r13, r15
	 *   Registers to be restored only:
	 * 	r8-r11: output value from the system call.
	 *
	 * During system call exit, scratch registers (including r15) are modified/cleared
	 * to prevent leaking bits from kernel to user level.
	 */
	DBG_FAULT(11)
	mov.m r16=IA64_KR(CURRENT)		// M2 r16 <- current task (12 cyc)
	MOV_FROM_IPSR(p0, r29)			// M2 (12 cyc)
	mov r31=pr				// I0 (2 cyc)

	MOV_FROM_IIM(r17)			// M2 (2 cyc)
	mov.m r27=ar.rsc			// M2 (12 cyc)
	mov r18=__IA64_BREAK_SYSCALL		// A

	mov.m ar.rsc=0				// M2
	mov.m r21=ar.fpsr			// M2 (12 cyc)
	mov r19=b6				// I0 (2 cyc)
	;;
	mov.m r23=ar.bspstore			// M2 (12 cyc)
	mov.m r24=ar.rnat			// M2 (5 cyc)
	mov.i r26=ar.pfs			// I0 (2 cyc)

	invala					// M0|1
	nop.m 0					// M
	mov r20=r1				// A			save r1

	nop.m 0
	movl r30=sys_call_table			// X

	MOV_FROM_IIP(r28)			// M2 (2 cyc)
	cmp.eq p0,p7=r18,r17			// I0 is this a system call?
(p7)	br.cond.spnt non_syscall		// B  no ->
	//
	// From this point on, we are definitely on the syscall-path
	// and we can use (non-banked) scratch registers.
	//
///////////////////////////////////////////////////////////////////////
	mov r1=r16				// A    move task-pointer to "addl"-addressable reg
	mov r2=r16				// A    setup r2 for ia64_syscall_setup
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16	// A	r9 = &current_thread_info()->flags

	adds r16=IA64_TASK_THREAD_ON_USTACK_OFFSET,r16
	adds r15=-1024,r15			// A    subtract 1024 from syscall number
	mov r3=NR_syscalls - 1
	;;
	ld1.bias r17=[r16]			// M0|1 r17 = current->thread.on_ustack flag
	ld4 r9=[r9]				// M0|1 r9 = current_thread_info()->flags
	extr.u r8=r29,41,2			// I0   extract ei field from cr.ipsr

	shladd r30=r15,3,r30			// A    r30 = sys_call_table + 8*(syscall-1024)
	addl r22=IA64_RBS_OFFSET,r1		// A    compute base of RBS
	cmp.leu p6,p7=r15,r3			// A    syscall number in range?
	;;

	lfetch.fault.excl.nt1 [r22]		// M0|1 prefetch RBS
(p6)	ld8 r30=[r30]				// M0|1 load address of syscall entry point
	tnat.nz.or p7,p0=r15			// I0	is syscall nr a NaT?

	mov.m ar.bspstore=r22			// M2   switch to kernel RBS
	cmp.eq p8,p9=2,r8			// A    isr.ei==2?
	;;

(p8)	mov r8=0				// A    clear ei to 0
(p7)	movl r30=sys_ni_syscall			// X

(p8)	adds r28=16,r28				// A    switch cr.iip to next bundle
(p9)	adds r8=1,r8				// A    increment ei to next slot
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	;;
	mov b6=r30				// I0   setup syscall handler branch reg early
#else
	nop.i 0
	;;
#endif

	mov.m r25=ar.unat			// M2 (5 cyc)
	dep r29=r8,r29,41,2			// I0   insert new ei into cr.ipsr
	adds r15=1024,r15			// A    restore original syscall number
	//
	// If any of the above loads miss in L1D, we'll stall here until
	// the data arrives.
	//
///////////////////////////////////////////////////////////////////////
	st1 [r16]=r0				// M2|3 clear current->thread.on_ustack flag
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	MOV_FROM_ITC(p0, p14, r30, r18)		// M    get cycle for accounting
#else
	mov b6=r30				// I0   setup syscall handler branch reg early
#endif
	cmp.eq pKStk,pUStk=r0,r17		// A    were we on kernel stacks already?

	and r9=_TIF_SYSCALL_TRACEAUDIT,r9	// A    mask trace or audit
	mov r18=ar.bsp				// M2 (12 cyc)
(pKStk)	br.cond.spnt .break_fixup		// B	we're already in kernel-mode -- fix up RBS
	;;
.back_from_break_fixup:
(pUStk)	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r1 // A    compute base of memory stack
	cmp.eq p14,p0=r9,r0			// A    are syscalls being traced/audited?
	br.call.sptk.many b7=ia64_syscall_setup	// B
1:
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	// mov.m r30=ar.itc is called in advance, and r13 is current
	add r16=TI_AC_STAMP+IA64_TASK_SIZE,r13	// A
	add r17=TI_AC_LEAVE+IA64_TASK_SIZE,r13	// A
(pKStk)	br.cond.spnt .skip_accounting		// B	unlikely skip
	;;
	ld8 r18=[r16],TI_AC_STIME-TI_AC_STAMP	// M  get last stamp
	ld8 r19=[r17],TI_AC_UTIME-TI_AC_LEAVE	// M  time at leave
	;;
	ld8 r20=[r16],TI_AC_STAMP-TI_AC_STIME	// M  cumulated stime
	ld8 r21=[r17]				// M  cumulated utime
	sub r22=r19,r18				// A  stime before leave
	;;
	st8 [r16]=r30,TI_AC_STIME-TI_AC_STAMP	// M  update stamp
	sub r18=r30,r19				// A  elapsed time in user
	;;
	add r20=r20,r22				// A  sum stime
	add r21=r21,r18				// A  sum utime
	;;
	st8 [r16]=r20				// M  update stime
	st8 [r17]=r21				// M  update utime
	;;
.skip_accounting:
#endif
	mov ar.rsc=0x3				// M2   set eager mode, pl 0, LE, loadrs=0
	nop 0
	BSW_1(r2, r14)				// B (6 cyc) regs are saved, switch to bank 1
	;;

	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r3, r16)	// M2	now it's safe to re-enable intr.-collection
						// M0   ensure interruption collection is on
	movl r3=ia64_ret_from_syscall		// X
	;;
	mov rp=r3				// I0   set the real return addr
(p10)	br.cond.spnt.many ia64_ret_from_syscall	// B    return if bad call-frame or r15 is a NaT

	SSM_PSR_I(p15, p15, r16)		// M2   restore psr.i
(p14)	br.call.sptk.many b6=b6			// B    invoke syscall-handker (ignore return addr)
	br.cond.spnt.many ia64_trace_syscall	// B	do syscall-tracing thingamagic
	// NOT REACHED
///////////////////////////////////////////////////////////////////////
	// On entry, we optimistically assumed that we're coming from user-space.
	// For the rare cases where a system-call is done from within the kernel,
	// we fix things up at this point:
.break_fixup:
	add r1=-IA64_PT_REGS_SIZE,sp		// A    allocate space for pt_regs structure
	mov ar.rnat=r24				// M2	restore kernel's AR.RNAT
	;;
	mov ar.bspstore=r23			// M2	restore kernel's AR.BSPSTORE
	br.cond.sptk .back_from_break_fixup
END(break_fault)

	.org ia64_ivt+0x3000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3000 Entry 12 (size 64 bundles) External Interrupt (4)
ENTRY(interrupt)
	/* interrupt handler has become too big to fit this area. */
	br.sptk.many __interrupt
END(interrupt)

	.org ia64_ivt+0x3400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3400 Entry 13 (size 64 bundles) Reserved
	DBG_FAULT(13)
	FAULT(13)

	.org ia64_ivt+0x3800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3800 Entry 14 (size 64 bundles) Reserved
	DBG_FAULT(14)
	FAULT(14)

	/*
	 * There is no particular reason for this code to be here, other than that
	 * there happens to be space here that would go unused otherwise.  If this
	 * fault ever gets "unreserved", simply moved the following code to a more
	 * suitable spot...
	 *
	 * ia64_syscall_setup() is a separate subroutine so that it can
	 *	allocate stacked registers so it can safely demine any
	 *	potential NaT values from the input registers.
	 *
	 * On entry:
	 *	- executing on bank 0 or bank 1 register set (doesn't matter)
	 *	-  r1: stack pointer
	 *	-  r2: current task pointer
	 *	-  r3: preserved
	 *	- r11: original contents (saved ar.pfs to be saved)
	 *	- r12: original contents (sp to be saved)
	 *	- r13: original contents (tp to be saved)
	 *	- r15: original contents (syscall # to be saved)
	 *	- r18: saved bsp (after switching to kernel stack)
	 *	- r19: saved b6
	 *	- r20: saved r1 (gp)
	 *	- r21: saved ar.fpsr
	 *	- r22: kernel's register backing store base (krbs_base)
	 *	- r23: saved ar.bspstore
	 *	- r24: saved ar.rnat
	 *	- r25: saved ar.unat
	 *	- r26: saved ar.pfs
	 *	- r27: saved ar.rsc
	 *	- r28: saved cr.iip
	 *	- r29: saved cr.ipsr
	 *	- r30: ar.itc for accounting (don't touch)
	 *	- r31: saved pr
	 *	-  b0: original contents (to be saved)
	 * On exit:
	 *	-  p10: TRUE if syscall is invoked with more than 8 out
	 *		registers or r15's Nat is true
	 *	-  r1: kernel's gp
	 *	-  r3: preserved (same as on entry)
	 *	-  r8: -EINVAL if p10 is true
	 *	- r12: points to kernel stack
	 *	- r13: points to current task
	 *	- r14: preserved (same as on entry)
	 *	- p13: preserved
	 *	- p15: TRUE if interrupts need to be re-enabled
	 *	- ar.fpsr: set to kernel settings
	 *	-  b6: preserved (same as on entry)
	 */
#ifdef __IA64_ASM_PARAVIRTUALIZED_NATIVE
GLOBAL_ENTRY(ia64_syscall_setup)
#if PT(B6) != 0
# error This code assumes that b6 is the first field in pt_regs.
#endif
	st8 [r1]=r19				// save b6
	add r16=PT(CR_IPSR),r1			// initialize first base pointer
	add r17=PT(R11),r1			// initialize second base pointer
	;;
	alloc r19=ar.pfs,8,0,0,0		// ensure in0-in7 are writable
	st8 [r16]=r29,PT(AR_PFS)-PT(CR_IPSR)	// save cr.ipsr
	tnat.nz p8,p0=in0

	st8.spill [r17]=r11,PT(CR_IIP)-PT(R11)	// save r11
	tnat.nz p9,p0=in1
(pKStk)	mov r18=r0				// make sure r18 isn't NaT
	;;

	st8 [r16]=r26,PT(CR_IFS)-PT(AR_PFS)	// save ar.pfs
	st8 [r17]=r28,PT(AR_UNAT)-PT(CR_IIP)	// save cr.iip
	mov r28=b0				// save b0 (2 cyc)
	;;

	st8 [r17]=r25,PT(AR_RSC)-PT(AR_UNAT)	// save ar.unat
	dep r19=0,r19,38,26			// clear all bits but 0..37 [I0]
(p8)	mov in0=-1
	;;

	st8 [r16]=r19,PT(AR_RNAT)-PT(CR_IFS)	// store ar.pfs.pfm in cr.ifs
	extr.u r11=r19,7,7	// I0		// get sol of ar.pfs
	and r8=0x7f,r19		// A		// get sof of ar.pfs

	st8 [r17]=r27,PT(AR_BSPSTORE)-PT(AR_RSC)// save ar.rsc
	tbit.nz p15,p0=r29,IA64_PSR_I_BIT // I0
(p9)	mov in1=-1
	;;

(pUStk) sub r18=r18,r22				// r18=RSE.ndirty*8
	tnat.nz p10,p0=in2
	add r11=8,r11
	;;
(pKStk) adds r16=PT(PR)-PT(AR_RNAT),r16		// skip over ar_rnat field
(pKStk) adds r17=PT(B0)-PT(AR_BSPSTORE),r17	// skip over ar_bspstore field
	tnat.nz p11,p0=in3
	;;
(p10)	mov in2=-1
	tnat.nz p12,p0=in4				// [I0]
(p11)	mov in3=-1
	;;
(pUStk) st8 [r16]=r24,PT(PR)-PT(AR_RNAT)	// save ar.rnat
(pUStk) st8 [r17]=r23,PT(B0)-PT(AR_BSPSTORE)	// save ar.bspstore
	shl r18=r18,16				// compute ar.rsc to be used for "loadrs"
	;;
	st8 [r16]=r31,PT(LOADRS)-PT(PR)		// save predicates
	st8 [r17]=r28,PT(R1)-PT(B0)		// save b0
	tnat.nz p13,p0=in5				// [I0]
	;;
	st8 [r16]=r18,PT(R12)-PT(LOADRS)	// save ar.rsc value for "loadrs"
	st8.spill [r17]=r20,PT(R13)-PT(R1)	// save original r1
(p12)	mov in4=-1
	;;

.mem.offset 0,0; st8.spill [r16]=r12,PT(AR_FPSR)-PT(R12)	// save r12
.mem.offset 8,0; st8.spill [r17]=r13,PT(R15)-PT(R13)		// save r13
(p13)	mov in5=-1
	;;
	st8 [r16]=r21,PT(R8)-PT(AR_FPSR)	// save ar.fpsr
	tnat.nz p13,p0=in6
	cmp.lt p10,p9=r11,r8	// frame size can't be more than local+8
	;;
	mov r8=1
(p9)	tnat.nz p10,p0=r15
	adds r12=-16,r1		// switch to kernel memory stack (with 16 bytes of scratch)

	st8.spill [r17]=r15			// save r15
	tnat.nz p8,p0=in7
	nop.i 0

	mov r13=r2				// establish `current'
	movl r1=__gp				// establish kernel global pointer
	;;
	st8 [r16]=r8		// ensure pt_regs.r8 != 0 (see handle_syscall_error)
(p13)	mov in6=-1
(p8)	mov in7=-1

	cmp.eq pSys,pNonSys=r0,r0		// set pSys=1, pNonSys=0
	movl r17=FPSR_DEFAULT
	;;
	mov.m ar.fpsr=r17			// set ar.fpsr to kernel default value
(p10)	mov r8=-EINVAL
	br.ret.sptk.many b7
END(ia64_syscall_setup)
#endif /* __IA64_ASM_PARAVIRTUALIZED_NATIVE */

	.org ia64_ivt+0x3c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x3c00 Entry 15 (size 64 bundles) Reserved
	DBG_FAULT(15)
	FAULT(15)

	.org ia64_ivt+0x4000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4000 Entry 16 (size 64 bundles) Reserved
	DBG_FAULT(16)
	FAULT(16)

#if defined(CONFIG_VIRT_CPU_ACCOUNTING) && defined(__IA64_ASM_PARAVIRTUALIZED_NATIVE)
	/*
	 * There is no particular reason for this code to be here, other than
	 * that there happens to be space here that would go unused otherwise.
	 * If this fault ever gets "unreserved", simply moved the following
	 * code to a more suitable spot...
	 *
	 * account_sys_enter is called from SAVE_MIN* macros if accounting is
	 * enabled and if the macro is entered from user mode.
	 */
GLOBAL_ENTRY(account_sys_enter)
	// mov.m r20=ar.itc is called in advance, and r13 is current
	add r16=TI_AC_STAMP+IA64_TASK_SIZE,r13
	add r17=TI_AC_LEAVE+IA64_TASK_SIZE,r13
	;;
	ld8 r18=[r16],TI_AC_STIME-TI_AC_STAMP	// time at last check in kernel
	ld8 r19=[r17],TI_AC_UTIME-TI_AC_LEAVE	// time at left from kernel
        ;;
	ld8 r23=[r16],TI_AC_STAMP-TI_AC_STIME	// cumulated stime
	ld8 r21=[r17]				// cumulated utime
	sub r22=r19,r18				// stime before leave kernel
	;;
	st8 [r16]=r20,TI_AC_STIME-TI_AC_STAMP	// update stamp
	sub r18=r20,r19				// elapsed time in user mode
	;;
	add r23=r23,r22				// sum stime
	add r21=r21,r18				// sum utime
	;;
	st8 [r16]=r23				// update stime
	st8 [r17]=r21				// update utime
	;;
	br.ret.sptk.many rp
END(account_sys_enter)
#endif

	.org ia64_ivt+0x4400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4400 Entry 17 (size 64 bundles) Reserved
	DBG_FAULT(17)
	FAULT(17)

	.org ia64_ivt+0x4800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4800 Entry 18 (size 64 bundles) Reserved
	DBG_FAULT(18)
	FAULT(18)

	.org ia64_ivt+0x4c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x4c00 Entry 19 (size 64 bundles) Reserved
	DBG_FAULT(19)
	FAULT(19)

//
// --- End of long entries, Beginning of short entries
//

	.org ia64_ivt+0x5000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5000 Entry 20 (size 16 bundles) Page Not Present (10,22,49)
ENTRY(page_not_present)
	DBG_FAULT(20)
	MOV_FROM_IFA(r16)
	RSM_PSR_DT
	/*
	 * The Linux page fault handler doesn't expect non-present pages to be in
	 * the TLB.  Flush the existing entry now, so we meet that expectation.
	 */
	mov r17=PAGE_SHIFT<<2
	;;
	ptc.l r16,r17
	;;
	mov r31=pr
	srlz.d
	br.sptk.many page_fault
END(page_not_present)

	.org ia64_ivt+0x5100
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5100 Entry 21 (size 16 bundles) Key Permission (13,25,52)
ENTRY(key_permission)
	DBG_FAULT(21)
	MOV_FROM_IFA(r16)
	RSM_PSR_DT
	mov r31=pr
	;;
	srlz.d
	br.sptk.many page_fault
END(key_permission)

	.org ia64_ivt+0x5200
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5200 Entry 22 (size 16 bundles) Instruction Access Rights (26)
ENTRY(iaccess_rights)
	DBG_FAULT(22)
	MOV_FROM_IFA(r16)
	RSM_PSR_DT
	mov r31=pr
	;;
	srlz.d
	br.sptk.many page_fault
END(iaccess_rights)

	.org ia64_ivt+0x5300
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5300 Entry 23 (size 16 bundles) Data Access Rights (14,53)
ENTRY(daccess_rights)
	DBG_FAULT(23)
	MOV_FROM_IFA(r16)
	RSM_PSR_DT
	mov r31=pr
	;;
	srlz.d
	br.sptk.many page_fault
END(daccess_rights)

	.org ia64_ivt+0x5400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5400 Entry 24 (size 16 bundles) General Exception (5,32,34,36,38,39)
ENTRY(general_exception)
	DBG_FAULT(24)
	MOV_FROM_ISR(r16)
	mov r31=pr
	;;
	cmp4.eq p6,p0=0,r16
(p6)	br.sptk.many dispatch_illegal_op_fault
	;;
	mov r19=24		// fault number
	br.sptk.many dispatch_to_fault_handler
END(general_exception)

	.org ia64_ivt+0x5500
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5500 Entry 25 (size 16 bundles) Disabled FP-Register (35)
ENTRY(disabled_fp_reg)
	DBG_FAULT(25)
	rsm psr.dfh		// ensure we can access fph
	;;
	srlz.d
	mov r31=pr
	mov r19=25
	br.sptk.many dispatch_to_fault_handler
END(disabled_fp_reg)

	.org ia64_ivt+0x5600
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5600 Entry 26 (size 16 bundles) Nat Consumption (11,23,37,50)
ENTRY(nat_consumption)
	DBG_FAULT(26)

	MOV_FROM_IPSR(p0, r16)
	MOV_FROM_ISR(r17)
	mov r31=pr				// save PR
	;;
	and r18=0xf,r17				// r18 = cr.ipsr.code{3:0}
	tbit.z p6,p0=r17,IA64_ISR_NA_BIT
	;;
	cmp.ne.or p6,p0=IA64_ISR_CODE_LFETCH,r18
	dep r16=-1,r16,IA64_PSR_ED_BIT,1
(p6)	br.cond.spnt 1f		// branch if (cr.ispr.na == 0 || cr.ipsr.code{3:0} != LFETCH)
	;;
	MOV_TO_IPSR(p0, r16, r18)
	mov pr=r31,-1
	;;
	RFI

1:	mov pr=r31,-1
	;;
	FAULT(26)
END(nat_consumption)

	.org ia64_ivt+0x5700
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5700 Entry 27 (size 16 bundles) Speculation (40)
ENTRY(speculation_vector)
	DBG_FAULT(27)
	/*
	 * A [f]chk.[as] instruction needs to take the branch to the recovery code but
	 * this part of the architecture is not implemented in hardware on some CPUs, such
	 * as Itanium.  Thus, in general we need to emulate the behavior.  IIM contains
	 * the relative target (not yet sign extended).  So after sign extending it we
	 * simply add it to IIP.  We also need to reset the EI field of the IPSR to zero,
	 * i.e., the slot to restart into.
	 *
	 * cr.imm contains zero_ext(imm21)
	 */
	MOV_FROM_IIM(r18)
	;;
	MOV_FROM_IIP(r17)
	shl r18=r18,43			// put sign bit in position (43=64-21)
	;;

	MOV_FROM_IPSR(p0, r16)
	shr r18=r18,39			// sign extend (39=43-4)
	;;

	add r17=r17,r18			// now add the offset
	;;
	MOV_TO_IIP(r17, r19)
	dep r16=0,r16,41,2		// clear EI
	;;

	MOV_TO_IPSR(p0, r16, r19)
	;;

	RFI
END(speculation_vector)

	.org ia64_ivt+0x5800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5800 Entry 28 (size 16 bundles) Reserved
	DBG_FAULT(28)
	FAULT(28)

	.org ia64_ivt+0x5900
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5900 Entry 29 (size 16 bundles) Debug (16,28,56)
ENTRY(debug_vector)
	DBG_FAULT(29)
	FAULT(29)
END(debug_vector)

	.org ia64_ivt+0x5a00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5a00 Entry 30 (size 16 bundles) Unaligned Reference (57)
ENTRY(unaligned_access)
	DBG_FAULT(30)
	mov r31=pr		// prepare to save predicates
	;;
	br.sptk.many dispatch_unaligned_handler
END(unaligned_access)

	.org ia64_ivt+0x5b00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5b00 Entry 31 (size 16 bundles) Unsupported Data Reference (57)
ENTRY(unsupported_data_reference)
	DBG_FAULT(31)
	FAULT(31)
END(unsupported_data_reference)

	.org ia64_ivt+0x5c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5c00 Entry 32 (size 16 bundles) Floating-Point Fault (64)
ENTRY(floating_point_fault)
	DBG_FAULT(32)
	FAULT(32)
END(floating_point_fault)

	.org ia64_ivt+0x5d00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5d00 Entry 33 (size 16 bundles) Floating Point Trap (66)
ENTRY(floating_point_trap)
	DBG_FAULT(33)
	FAULT(33)
END(floating_point_trap)

	.org ia64_ivt+0x5e00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5e00 Entry 34 (size 16 bundles) Lower Privilege Transfer Trap (66)
ENTRY(lower_privilege_trap)
	DBG_FAULT(34)
	FAULT(34)
END(lower_privilege_trap)

	.org ia64_ivt+0x5f00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x5f00 Entry 35 (size 16 bundles) Taken Branch Trap (68)
ENTRY(taken_branch_trap)
	DBG_FAULT(35)
	FAULT(35)
END(taken_branch_trap)

	.org ia64_ivt+0x6000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6000 Entry 36 (size 16 bundles) Single Step Trap (69)
ENTRY(single_step_trap)
	DBG_FAULT(36)
	FAULT(36)
END(single_step_trap)

	.org ia64_ivt+0x6100
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6100 Entry 37 (size 16 bundles) Reserved
	DBG_FAULT(37)
	FAULT(37)

	.org ia64_ivt+0x6200
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6200 Entry 38 (size 16 bundles) Reserved
	DBG_FAULT(38)
	FAULT(38)

	.org ia64_ivt+0x6300
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6300 Entry 39 (size 16 bundles) Reserved
	DBG_FAULT(39)
	FAULT(39)

	.org ia64_ivt+0x6400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6400 Entry 40 (size 16 bundles) Reserved
	DBG_FAULT(40)
	FAULT(40)

	.org ia64_ivt+0x6500
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6500 Entry 41 (size 16 bundles) Reserved
	DBG_FAULT(41)
	FAULT(41)

	.org ia64_ivt+0x6600
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6600 Entry 42 (size 16 bundles) Reserved
	DBG_FAULT(42)
	FAULT(42)

	.org ia64_ivt+0x6700
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6700 Entry 43 (size 16 bundles) Reserved
	DBG_FAULT(43)
	FAULT(43)

	.org ia64_ivt+0x6800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6800 Entry 44 (size 16 bundles) Reserved
	DBG_FAULT(44)
	FAULT(44)

	.org ia64_ivt+0x6900
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6900 Entry 45 (size 16 bundles) IA-32 Exeception (17,18,29,41,42,43,44,58,60,61,62,72,73,75,76,77)
ENTRY(ia32_exception)
	DBG_FAULT(45)
	FAULT(45)
END(ia32_exception)

	.org ia64_ivt+0x6a00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6a00 Entry 46 (size 16 bundles) IA-32 Intercept  (30,31,59,70,71)
ENTRY(ia32_intercept)
	DBG_FAULT(46)
	FAULT(46)
END(ia32_intercept)

	.org ia64_ivt+0x6b00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6b00 Entry 47 (size 16 bundles) IA-32 Interrupt  (74)
ENTRY(ia32_interrupt)
	DBG_FAULT(47)
	FAULT(47)
END(ia32_interrupt)

	.org ia64_ivt+0x6c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6c00 Entry 48 (size 16 bundles) Reserved
	DBG_FAULT(48)
	FAULT(48)

	.org ia64_ivt+0x6d00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6d00 Entry 49 (size 16 bundles) Reserved
	DBG_FAULT(49)
	FAULT(49)

	.org ia64_ivt+0x6e00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6e00 Entry 50 (size 16 bundles) Reserved
	DBG_FAULT(50)
	FAULT(50)

	.org ia64_ivt+0x6f00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x6f00 Entry 51 (size 16 bundles) Reserved
	DBG_FAULT(51)
	FAULT(51)

	.org ia64_ivt+0x7000
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7000 Entry 52 (size 16 bundles) Reserved
	DBG_FAULT(52)
	FAULT(52)

	.org ia64_ivt+0x7100
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7100 Entry 53 (size 16 bundles) Reserved
	DBG_FAULT(53)
	FAULT(53)

	.org ia64_ivt+0x7200
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7200 Entry 54 (size 16 bundles) Reserved
	DBG_FAULT(54)
	FAULT(54)

	.org ia64_ivt+0x7300
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7300 Entry 55 (size 16 bundles) Reserved
	DBG_FAULT(55)
	FAULT(55)

	.org ia64_ivt+0x7400
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7400 Entry 56 (size 16 bundles) Reserved
	DBG_FAULT(56)
	FAULT(56)

	.org ia64_ivt+0x7500
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7500 Entry 57 (size 16 bundles) Reserved
	DBG_FAULT(57)
	FAULT(57)

	.org ia64_ivt+0x7600
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7600 Entry 58 (size 16 bundles) Reserved
	DBG_FAULT(58)
	FAULT(58)

	.org ia64_ivt+0x7700
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7700 Entry 59 (size 16 bundles) Reserved
	DBG_FAULT(59)
	FAULT(59)

	.org ia64_ivt+0x7800
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7800 Entry 60 (size 16 bundles) Reserved
	DBG_FAULT(60)
	FAULT(60)

	.org ia64_ivt+0x7900
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7900 Entry 61 (size 16 bundles) Reserved
	DBG_FAULT(61)
	FAULT(61)

	.org ia64_ivt+0x7a00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7a00 Entry 62 (size 16 bundles) Reserved
	DBG_FAULT(62)
	FAULT(62)

	.org ia64_ivt+0x7b00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7b00 Entry 63 (size 16 bundles) Reserved
	DBG_FAULT(63)
	FAULT(63)

	.org ia64_ivt+0x7c00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7c00 Entry 64 (size 16 bundles) Reserved
	DBG_FAULT(64)
	FAULT(64)

	.org ia64_ivt+0x7d00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7d00 Entry 65 (size 16 bundles) Reserved
	DBG_FAULT(65)
	FAULT(65)

	.org ia64_ivt+0x7e00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7e00 Entry 66 (size 16 bundles) Reserved
	DBG_FAULT(66)
	FAULT(66)

	.org ia64_ivt+0x7f00
/////////////////////////////////////////////////////////////////////////////////////////
// 0x7f00 Entry 67 (size 16 bundles) Reserved
	DBG_FAULT(67)
	FAULT(67)

	//-----------------------------------------------------------------------------------
	// call do_page_fault (predicates are in r31, psr.dt may be off, r16 is faulting address)
ENTRY(page_fault)
	SSM_PSR_DT_AND_SRLZ_I
	;;
	SAVE_MIN_WITH_COVER
	alloc r15=ar.pfs,0,0,3,0
	MOV_FROM_IFA(out0)
	MOV_FROM_ISR(out1)
	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r14, r3)
	adds r3=8,r2				// set up second base pointer
	SSM_PSR_I(p15, p15, r14)		// restore psr.i
	movl r14=ia64_leave_kernel
	;;
	SAVE_REST
	mov rp=r14
	;;
	adds out2=16,r12			// out2 = pointer to pt_regs
	br.call.sptk.many b6=ia64_do_page_fault	// ignore return address
END(page_fault)

ENTRY(non_syscall)
	mov ar.rsc=r27			// restore ar.rsc before SAVE_MIN_WITH_COVER
	;;
	SAVE_MIN_WITH_COVER

	// There is no particular reason for this code to be here, other than that
	// there happens to be space here that would go unused otherwise.  If this
	// fault ever gets "unreserved", simply moved the following code to a more
	// suitable spot...

	alloc r14=ar.pfs,0,0,2,0
	MOV_FROM_IIM(out0)
	add out1=16,sp
	adds r3=8,r2			// set up second base pointer for SAVE_REST

	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r15, r24)
					// guarantee that interruption collection is on
	SSM_PSR_I(p15, p15, r15)	// restore psr.i
	movl r15=ia64_leave_kernel
	;;
	SAVE_REST
	mov rp=r15
	;;
	br.call.sptk.many b6=ia64_bad_break	// avoid WAW on CFM and ignore return addr
END(non_syscall)

ENTRY(__interrupt)
	DBG_FAULT(12)
	mov r31=pr		// prepare to save predicates
	;;
	SAVE_MIN_WITH_COVER	// uses r31; defines r2 and r3
	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r3, r14)
				// ensure everybody knows psr.ic is back on
	adds r3=8,r2		// set up second base pointer for SAVE_REST
	;;
	SAVE_REST
	;;
	MCA_RECOVER_RANGE(interrupt)
	alloc r14=ar.pfs,0,0,2,0 // must be first in an insn group
	MOV_FROM_IVR(out0, r8)	// pass cr.ivr as first arg
	add out1=16,sp		// pass pointer to pt_regs as second arg
	;;
	srlz.d			// make sure we see the effect of cr.ivr
	movl r14=ia64_leave_kernel
	;;
	mov rp=r14
	br.call.sptk.many b6=ia64_handle_irq
END(__interrupt)

	/*
	 * There is no particular reason for this code to be here, other than that
	 * there happens to be space here that would go unused otherwise.  If this
	 * fault ever gets "unreserved", simply moved the following code to a more
	 * suitable spot...
	 */

ENTRY(dispatch_unaligned_handler)
	SAVE_MIN_WITH_COVER
	;;
	alloc r14=ar.pfs,0,0,2,0		// now it's safe (must be first in insn group!)
	MOV_FROM_IFA(out0)
	adds out1=16,sp

	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r3, r24)
						// guarantee that interruption collection is on
	SSM_PSR_I(p15, p15, r3)			// restore psr.i
	adds r3=8,r2				// set up second base pointer
	;;
	SAVE_REST
	movl r14=ia64_leave_kernel
	;;
	mov rp=r14
	br.sptk.many ia64_prepare_handle_unaligned
END(dispatch_unaligned_handler)

	/*
	 * There is no particular reason for this code to be here, other than that
	 * there happens to be space here that would go unused otherwise.  If this
	 * fault ever gets "unreserved", simply moved the following code to a more
	 * suitable spot...
	 */

ENTRY(dispatch_to_fault_handler)
	/*
	 * Input:
	 *	psr.ic:	off
	 *	r19:	fault vector number (e.g., 24 for General Exception)
	 *	r31:	contains saved predicates (pr)
	 */
	SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,5,0
	MOV_FROM_ISR(out1)
	MOV_FROM_IFA(out2)
	MOV_FROM_IIM(out3)
	MOV_FROM_ITIR(out4)
	;;
	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r3, out0)
						// guarantee that interruption collection is on
	mov out0=r15
	;;
	SSM_PSR_I(p15, p15, r3)			// restore psr.i
	adds r3=8,r2				// set up second base pointer for SAVE_REST
	;;
	SAVE_REST
	movl r14=ia64_leave_kernel
	;;
	mov rp=r14
	br.call.sptk.many b6=ia64_fault
END(dispatch_to_fault_handler)

	/*
	 * Squatting in this space ...
	 *
	 * This special case dispatcher for illegal operation faults allows preserved
	 * registers to be modified through a callback function (asm only) that is handed
	 * back from the fault handler in r8. Up to three arguments can be passed to the
	 * callback function by returning an aggregate with the callback as its first
	 * element, followed by the arguments.
	 */
ENTRY(dispatch_illegal_op_fault)
	.prologue
	.body
	SAVE_MIN_WITH_COVER
	SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(r3, r24)
				// guarantee that interruption collection is on
	;;
	SSM_PSR_I(p15, p15, r3)	// restore psr.i
	adds r3=8,r2	// set up second base pointer for SAVE_REST
	;;
	alloc r14=ar.pfs,0,0,1,0	// must be first in insn group
	mov out0=ar.ec
	;;
	SAVE_REST
	PT_REGS_UNWIND_INFO(0)
	;;
	br.call.sptk.many rp=ia64_illegal_op_fault
.ret0:	;;
	alloc r14=ar.pfs,0,0,3,0	// must be first in insn group
	mov out0=r9
	mov out1=r10
	mov out2=r11
	movl r15=ia64_leave_kernel
	;;
	mov rp=r15
	mov b6=r8
	;;
	cmp.ne p6,p0=0,r8
(p6)	br.call.dpnt.many b6=b6		// call returns to ia64_leave_kernel
	br.sptk.many ia64_leave_kernel
END(dispatch_illegal_op_fault)
