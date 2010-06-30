/*
 * arch/ia64/kvm/vmm_ivt.S
 *
 * Copyright (C) 1998-2001, 2003 Hewlett-Packard Co
 *      Stephane Eranian <eranian@hpl.hp.com>
 *      David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 2000, 2002-2003 Intel Co
 *      Asit Mallick <asit.k.mallick@intel.com>
 *      Suresh Siddha <suresh.b.siddha@intel.com>
 *      Kenneth Chen <kenneth.w.chen@intel.com>
 *      Fenghua Yu <fenghua.yu@intel.com>
 *
 *
 * 00/08/23 Asit Mallick <asit.k.mallick@intel.com> TLB handling
 * for SMP
 * 00/12/20 David Mosberger-Tang <davidm@hpl.hp.com> DTLB/ITLB
 * handler now uses virtual PT.
 *
 * 07/6/20 Xuefei Xu  (Anthony Xu) (anthony.xu@intel.com)
 *              Supporting Intel virtualization architecture
 *
 */

/*
 * This file defines the interruption vector table used by the CPU.
 * It does not include one entry per possible cause of interruption.
 *
 * The first 20 entries of the table contain 64 bundles each while the
 * remaining 48 entries contain only 16 bundles each.
 *
 * The 64 bundles are used to allow inlining the whole handler for
 * critical
 * interruptions like TLB misses.
 *
 *  For each entry, the comment is as follows:
 *
 *              // 0x1c00 Entry 7 (size 64 bundles) Data Key Miss
 *              (12,51)
 *  entry offset ----/     /         /                  /
 *  /
 *  entry number ---------/         /                  /
 *  /
 *  size of the entry -------------/                  /
 *  /
 *  vector name -------------------------------------/
 *  /
 *  interruptions triggering this vector
 *  ----------------------/
 *
 * The table is 32KB in size and must be aligned on 32KB
 * boundary.
 * (The CPU ignores the 15 lower bits of the address)
 *
 * Table is based upon EAS2.6 (Oct 1999)
 */


#include <asm/asmmacro.h>
#include <asm/cache.h>
#include <asm/pgtable.h>

#include "asm-offsets.h"
#include "vcpu.h"
#include "kvm_minstate.h"
#include "vti.h"

#if 1
# define PSR_DEFAULT_BITS   psr.ac
#else
# define PSR_DEFAULT_BITS   0
#endif

#define KVM_FAULT(n)    \
	kvm_fault_##n:;          \
	mov r19=n;;          \
	br.sptk.many kvm_vmm_panic;         \
	;;                  \

#define KVM_REFLECT(n)    \
	mov r31=pr;           \
	mov r19=n;       /* prepare to save predicates */ \
	mov r29=cr.ipsr;      \
	;;      \
	tbit.z p6,p7=r29,IA64_PSR_VM_BIT;       \
(p7)	br.sptk.many kvm_dispatch_reflection;        \
	br.sptk.many kvm_vmm_panic;      \

GLOBAL_ENTRY(kvm_vmm_panic)
	KVM_SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,1,0
	mov out0=r15
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i    // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	KVM_SAVE_REST
	mov rp=r14
	;;
	br.call.sptk.many b6=vmm_panic_handler;
END(kvm_vmm_panic)

    .section .text..ivt,"ax"

    .align 32768    // align on 32KB boundary
    .global kvm_ia64_ivt
kvm_ia64_ivt:
///////////////////////////////////////////////////////////////
// 0x0000 Entry 0 (size 64 bundles) VHPT Translation (8,20,47)
ENTRY(kvm_vhpt_miss)
	KVM_FAULT(0)
END(kvm_vhpt_miss)

    .org kvm_ia64_ivt+0x400
////////////////////////////////////////////////////////////////
// 0x0400 Entry 1 (size 64 bundles) ITLB (21)
ENTRY(kvm_itlb_miss)
	mov r31 = pr
	mov r29=cr.ipsr;
	;;
	tbit.z p6,p7=r29,IA64_PSR_VM_BIT;
(p6)	br.sptk kvm_alt_itlb_miss
	mov r19 = 1
	br.sptk kvm_itlb_miss_dispatch
	KVM_FAULT(1);
END(kvm_itlb_miss)

    .org kvm_ia64_ivt+0x0800
//////////////////////////////////////////////////////////////////
// 0x0800 Entry 2 (size 64 bundles) DTLB (9,48)
ENTRY(kvm_dtlb_miss)
	mov r31 = pr
	mov r29=cr.ipsr;
	;;
	tbit.z p6,p7=r29,IA64_PSR_VM_BIT;
(p6)	br.sptk kvm_alt_dtlb_miss
	br.sptk kvm_dtlb_miss_dispatch
END(kvm_dtlb_miss)

     .org kvm_ia64_ivt+0x0c00
////////////////////////////////////////////////////////////////////
// 0x0c00 Entry 3 (size 64 bundles) Alt ITLB (19)
ENTRY(kvm_alt_itlb_miss)
	mov r16=cr.ifa    // get address that caused the TLB miss
	;;
	movl r17=PAGE_KERNEL
	mov r24=cr.ipsr
	movl r19=(((1 << IA64_MAX_PHYS_BITS) - 1) & ~0xfff)
	;;
	and r19=r19,r16     // clear ed, reserved bits, and PTE control bits
	;;
	or r19=r17,r19      // insert PTE control bits into r19
	;;
	movl r20=IA64_GRANULE_SHIFT<<2
	;;
	mov cr.itir=r20
	;;
	itc.i r19		// insert the TLB entry
	mov pr=r31,-1
	rfi
END(kvm_alt_itlb_miss)

    .org kvm_ia64_ivt+0x1000
/////////////////////////////////////////////////////////////////////
// 0x1000 Entry 4 (size 64 bundles) Alt DTLB (7,46)
ENTRY(kvm_alt_dtlb_miss)
	mov r16=cr.ifa		// get address that caused the TLB miss
	;;
	movl r17=PAGE_KERNEL
	movl r19=(((1 << IA64_MAX_PHYS_BITS) - 1) & ~0xfff)
	mov r24=cr.ipsr
	;;
	and r19=r19,r16     // clear ed, reserved bits, and PTE control bits
	;;
	or r19=r19,r17	// insert PTE control bits into r19
	;;
	movl r20=IA64_GRANULE_SHIFT<<2
	;;
	mov cr.itir=r20
	;;
	itc.d r19		// insert the TLB entry
	mov pr=r31,-1
	rfi
END(kvm_alt_dtlb_miss)

    .org kvm_ia64_ivt+0x1400
//////////////////////////////////////////////////////////////////////
// 0x1400 Entry 5 (size 64 bundles) Data nested TLB (6,45)
ENTRY(kvm_nested_dtlb_miss)
	KVM_FAULT(5)
END(kvm_nested_dtlb_miss)

    .org kvm_ia64_ivt+0x1800
/////////////////////////////////////////////////////////////////////
// 0x1800 Entry 6 (size 64 bundles) Instruction Key Miss (24)
ENTRY(kvm_ikey_miss)
	KVM_REFLECT(6)
END(kvm_ikey_miss)

    .org kvm_ia64_ivt+0x1c00
/////////////////////////////////////////////////////////////////////
// 0x1c00 Entry 7 (size 64 bundles) Data Key Miss (12,51)
ENTRY(kvm_dkey_miss)
	KVM_REFLECT(7)
END(kvm_dkey_miss)

    .org kvm_ia64_ivt+0x2000
////////////////////////////////////////////////////////////////////
// 0x2000 Entry 8 (size 64 bundles) Dirty-bit (54)
ENTRY(kvm_dirty_bit)
	KVM_REFLECT(8)
END(kvm_dirty_bit)

    .org kvm_ia64_ivt+0x2400
////////////////////////////////////////////////////////////////////
// 0x2400 Entry 9 (size 64 bundles) Instruction Access-bit (27)
ENTRY(kvm_iaccess_bit)
	KVM_REFLECT(9)
END(kvm_iaccess_bit)

    .org kvm_ia64_ivt+0x2800
///////////////////////////////////////////////////////////////////
// 0x2800 Entry 10 (size 64 bundles) Data Access-bit (15,55)
ENTRY(kvm_daccess_bit)
	KVM_REFLECT(10)
END(kvm_daccess_bit)

    .org kvm_ia64_ivt+0x2c00
/////////////////////////////////////////////////////////////////
// 0x2c00 Entry 11 (size 64 bundles) Break instruction (33)
ENTRY(kvm_break_fault)
	mov r31=pr
	mov r19=11
	mov r29=cr.ipsr
	;;
	KVM_SAVE_MIN_WITH_COVER_R19
	;;
	alloc r14=ar.pfs,0,0,4,0 //(must be first in insn group!)
	mov out0=cr.ifa
	mov out2=cr.isr     // FIXME: pity to make this slow access twice
	mov out3=cr.iim     // FIXME: pity to make this slow access twice
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i         // guarantee that interruption collection is on
	;;
	(p15)ssm psr.i               // restore psr.i
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	KVM_SAVE_REST
	mov rp=r14
	;;
	adds out1=16,sp
	br.call.sptk.many b6=kvm_ia64_handle_break
	;;
END(kvm_break_fault)

    .org kvm_ia64_ivt+0x3000
/////////////////////////////////////////////////////////////////
// 0x3000 Entry 12 (size 64 bundles) External Interrupt (4)
ENTRY(kvm_interrupt)
	mov r31=pr		// prepare to save predicates
	mov r19=12
	mov r29=cr.ipsr
	;;
	tbit.z p6,p7=r29,IA64_PSR_VM_BIT
	tbit.z p0,p15=r29,IA64_PSR_I_BIT
	;;
(p7)	br.sptk kvm_dispatch_interrupt
	;;
	mov r27=ar.rsc		/* M */
	mov r20=r1			/* A */
	mov r25=ar.unat		/* M */
	mov r26=ar.pfs		/* I */
	mov r28=cr.iip		/* M */
	cover			/* B (or nothing) */
	;;
	mov r1=sp
	;;
	invala			/* M */
	mov r30=cr.ifs
	;;
	addl r1=-VMM_PT_REGS_SIZE,r1
	;;
	adds r17=2*L1_CACHE_BYTES,r1	/* really: biggest cache-line size */
	adds r16=PT(CR_IPSR),r1
	;;
	lfetch.fault.excl.nt1 [r17],L1_CACHE_BYTES
	st8 [r16]=r29			/* save cr.ipsr */
	;;
	lfetch.fault.excl.nt1 [r17]
	mov r29=b0
	;;
	adds r16=PT(R8),r1  	/* initialize first base pointer */
	adds r17=PT(R9),r1  	/* initialize second base pointer */
	mov r18=r0      		/* make sure r18 isn't NaT */
	;;
.mem.offset 0,0; st8.spill [r16]=r8,16
.mem.offset 8,0; st8.spill [r17]=r9,16
        ;;
.mem.offset 0,0; st8.spill [r16]=r10,24
.mem.offset 8,0; st8.spill [r17]=r11,24
        ;;
	st8 [r16]=r28,16		/* save cr.iip */
	st8 [r17]=r30,16		/* save cr.ifs */
	mov r8=ar.fpsr		/* M */
	mov r9=ar.csd
	mov r10=ar.ssd
	movl r11=FPSR_DEFAULT	/* L-unit */
	;;
	st8 [r16]=r25,16		/* save ar.unat */
	st8 [r17]=r26,16		/* save ar.pfs */
	shl r18=r18,16		/* compute ar.rsc to be used for "loadrs" */
	;;
	st8 [r16]=r27,16		/* save ar.rsc */
	adds r17=16,r17		/* skip over ar_rnat field */
	;;
	st8 [r17]=r31,16		/* save predicates */
	adds r16=16,r16		/* skip over ar_bspstore field */
	;;
	st8 [r16]=r29,16		/* save b0 */
	st8 [r17]=r18,16		/* save ar.rsc value for "loadrs" */
	;;
.mem.offset 0,0; st8.spill [r16]=r20,16    /* save original r1 */
.mem.offset 8,0; st8.spill [r17]=r12,16
	adds r12=-16,r1
	/* switch to kernel memory stack (with 16 bytes of scratch) */
	;;
.mem.offset 0,0; st8.spill [r16]=r13,16
.mem.offset 8,0; st8.spill [r17]=r8,16 /* save ar.fpsr */
	;;
.mem.offset 0,0; st8.spill [r16]=r15,16
.mem.offset 8,0; st8.spill [r17]=r14,16
	dep r14=-1,r0,60,4
	;;
.mem.offset 0,0; st8.spill [r16]=r2,16
.mem.offset 8,0; st8.spill [r17]=r3,16
	adds r2=VMM_PT_REGS_R16_OFFSET,r1
	adds r14 = VMM_VCPU_GP_OFFSET,r13
	;;
	mov r8=ar.ccv
	ld8 r14 = [r14]
	;;
	mov r1=r14       /* establish kernel global pointer */
	;;                                          \
	bsw.1
	;;
	alloc r14=ar.pfs,0,0,1,0	// must be first in an insn group
	mov out0=r13
	;;
	ssm psr.ic
	;;
	srlz.i
	;;
	//(p15) ssm psr.i
	adds r3=8,r2		// set up second base pointer for SAVE_REST
	srlz.i			// ensure everybody knows psr.ic is back on
	;;
.mem.offset 0,0; st8.spill [r2]=r16,16
.mem.offset 8,0; st8.spill [r3]=r17,16
	;;
.mem.offset 0,0; st8.spill [r2]=r18,16
.mem.offset 8,0; st8.spill [r3]=r19,16
	;;
.mem.offset 0,0; st8.spill [r2]=r20,16
.mem.offset 8,0; st8.spill [r3]=r21,16
	mov r18=b6
	;;
.mem.offset 0,0; st8.spill [r2]=r22,16
.mem.offset 8,0; st8.spill [r3]=r23,16
	mov r19=b7
	;;
.mem.offset 0,0; st8.spill [r2]=r24,16
.mem.offset 8,0; st8.spill [r3]=r25,16
	;;
.mem.offset 0,0; st8.spill [r2]=r26,16
.mem.offset 8,0; st8.spill [r3]=r27,16
	;;
.mem.offset 0,0; st8.spill [r2]=r28,16
.mem.offset 8,0; st8.spill [r3]=r29,16
	;;
.mem.offset 0,0; st8.spill [r2]=r30,16
.mem.offset 8,0; st8.spill [r3]=r31,32
	;;
	mov ar.fpsr=r11       /* M-unit */
	st8 [r2]=r8,8         /* ar.ccv */
	adds r24=PT(B6)-PT(F7),r3
	;;
	stf.spill [r2]=f6,32
	stf.spill [r3]=f7,32
	;;
	stf.spill [r2]=f8,32
	stf.spill [r3]=f9,32
	;;
	stf.spill [r2]=f10
	stf.spill [r3]=f11
	adds r25=PT(B7)-PT(F11),r3
	;;
	st8 [r24]=r18,16       /* b6 */
	st8 [r25]=r19,16       /* b7 */
	;;
	st8 [r24]=r9           /* ar.csd */
	st8 [r25]=r10          /* ar.ssd */
	;;
	srlz.d		// make sure we see the effect of cr.ivr
	addl r14=@gprel(ia64_leave_nested),gp
	;;
	mov rp=r14
	br.call.sptk.many b6=kvm_ia64_handle_irq
	;;
END(kvm_interrupt)

    .global kvm_dispatch_vexirq
    .org kvm_ia64_ivt+0x3400
//////////////////////////////////////////////////////////////////////
// 0x3400 Entry 13 (size 64 bundles) Reserved
ENTRY(kvm_virtual_exirq)
	mov r31=pr
	mov r19=13
	mov r30 =r0
	;;
kvm_dispatch_vexirq:
	cmp.eq p6,p0 = 1,r30
	;;
(p6)	add r29 = VMM_VCPU_SAVED_GP_OFFSET,r21
	;;
(p6)	ld8 r1 = [r29]
	;;
	KVM_SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,1,0
	mov out0=r13

	ssm psr.ic
	;;
	srlz.i // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.i
	adds r3=8,r2                // set up second base pointer
	;;
	KVM_SAVE_REST
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	mov rp=r14
	br.call.sptk.many b6=kvm_vexirq
END(kvm_virtual_exirq)

    .org kvm_ia64_ivt+0x3800
/////////////////////////////////////////////////////////////////////
// 0x3800 Entry 14 (size 64 bundles) Reserved
	KVM_FAULT(14)
	// this code segment is from 2.6.16.13

    .org kvm_ia64_ivt+0x3c00
///////////////////////////////////////////////////////////////////////
// 0x3c00 Entry 15 (size 64 bundles) Reserved
	KVM_FAULT(15)

    .org kvm_ia64_ivt+0x4000
///////////////////////////////////////////////////////////////////////
// 0x4000 Entry 16 (size 64 bundles) Reserved
	KVM_FAULT(16)

    .org kvm_ia64_ivt+0x4400
//////////////////////////////////////////////////////////////////////
// 0x4400 Entry 17 (size 64 bundles) Reserved
	KVM_FAULT(17)

    .org kvm_ia64_ivt+0x4800
//////////////////////////////////////////////////////////////////////
// 0x4800 Entry 18 (size 64 bundles) Reserved
	KVM_FAULT(18)

    .org kvm_ia64_ivt+0x4c00
//////////////////////////////////////////////////////////////////////
// 0x4c00 Entry 19 (size 64 bundles) Reserved
	KVM_FAULT(19)

    .org kvm_ia64_ivt+0x5000
//////////////////////////////////////////////////////////////////////
// 0x5000 Entry 20 (size 16 bundles) Page Not Present
ENTRY(kvm_page_not_present)
	KVM_REFLECT(20)
END(kvm_page_not_present)

    .org kvm_ia64_ivt+0x5100
///////////////////////////////////////////////////////////////////////
// 0x5100 Entry 21 (size 16 bundles) Key Permission vector
ENTRY(kvm_key_permission)
	KVM_REFLECT(21)
END(kvm_key_permission)

    .org kvm_ia64_ivt+0x5200
//////////////////////////////////////////////////////////////////////
// 0x5200 Entry 22 (size 16 bundles) Instruction Access Rights (26)
ENTRY(kvm_iaccess_rights)
	KVM_REFLECT(22)
END(kvm_iaccess_rights)

    .org kvm_ia64_ivt+0x5300
//////////////////////////////////////////////////////////////////////
// 0x5300 Entry 23 (size 16 bundles) Data Access Rights (14,53)
ENTRY(kvm_daccess_rights)
	KVM_REFLECT(23)
END(kvm_daccess_rights)

    .org kvm_ia64_ivt+0x5400
/////////////////////////////////////////////////////////////////////
// 0x5400 Entry 24 (size 16 bundles) General Exception (5,32,34,36,38,39)
ENTRY(kvm_general_exception)
	KVM_REFLECT(24)
	KVM_FAULT(24)
END(kvm_general_exception)

    .org kvm_ia64_ivt+0x5500
//////////////////////////////////////////////////////////////////////
// 0x5500 Entry 25 (size 16 bundles) Disabled FP-Register (35)
ENTRY(kvm_disabled_fp_reg)
	KVM_REFLECT(25)
END(kvm_disabled_fp_reg)

    .org kvm_ia64_ivt+0x5600
////////////////////////////////////////////////////////////////////
// 0x5600 Entry 26 (size 16 bundles) Nat Consumption (11,23,37,50)
ENTRY(kvm_nat_consumption)
	KVM_REFLECT(26)
END(kvm_nat_consumption)

    .org kvm_ia64_ivt+0x5700
/////////////////////////////////////////////////////////////////////
// 0x5700 Entry 27 (size 16 bundles) Speculation (40)
ENTRY(kvm_speculation_vector)
	KVM_REFLECT(27)
END(kvm_speculation_vector)

    .org kvm_ia64_ivt+0x5800
/////////////////////////////////////////////////////////////////////
// 0x5800 Entry 28 (size 16 bundles) Reserved
	KVM_FAULT(28)

    .org kvm_ia64_ivt+0x5900
///////////////////////////////////////////////////////////////////
// 0x5900 Entry 29 (size 16 bundles) Debug (16,28,56)
ENTRY(kvm_debug_vector)
	KVM_FAULT(29)
END(kvm_debug_vector)

    .org kvm_ia64_ivt+0x5a00
///////////////////////////////////////////////////////////////
// 0x5a00 Entry 30 (size 16 bundles) Unaligned Reference (57)
ENTRY(kvm_unaligned_access)
	KVM_REFLECT(30)
END(kvm_unaligned_access)

    .org kvm_ia64_ivt+0x5b00
//////////////////////////////////////////////////////////////////////
// 0x5b00 Entry 31 (size 16 bundles) Unsupported Data Reference (57)
ENTRY(kvm_unsupported_data_reference)
	KVM_REFLECT(31)
END(kvm_unsupported_data_reference)

    .org kvm_ia64_ivt+0x5c00
////////////////////////////////////////////////////////////////////
// 0x5c00 Entry 32 (size 16 bundles) Floating Point FAULT (65)
ENTRY(kvm_floating_point_fault)
	KVM_REFLECT(32)
END(kvm_floating_point_fault)

    .org kvm_ia64_ivt+0x5d00
/////////////////////////////////////////////////////////////////////
// 0x5d00 Entry 33 (size 16 bundles) Floating Point Trap (66)
ENTRY(kvm_floating_point_trap)
	KVM_REFLECT(33)
END(kvm_floating_point_trap)

    .org kvm_ia64_ivt+0x5e00
//////////////////////////////////////////////////////////////////////
// 0x5e00 Entry 34 (size 16 bundles) Lower Privilege Transfer Trap (66)
ENTRY(kvm_lower_privilege_trap)
	KVM_REFLECT(34)
END(kvm_lower_privilege_trap)

    .org kvm_ia64_ivt+0x5f00
//////////////////////////////////////////////////////////////////////
// 0x5f00 Entry 35 (size 16 bundles) Taken Branch Trap (68)
ENTRY(kvm_taken_branch_trap)
	KVM_REFLECT(35)
END(kvm_taken_branch_trap)

    .org kvm_ia64_ivt+0x6000
////////////////////////////////////////////////////////////////////
// 0x6000 Entry 36 (size 16 bundles) Single Step Trap (69)
ENTRY(kvm_single_step_trap)
	KVM_REFLECT(36)
END(kvm_single_step_trap)
    .global kvm_virtualization_fault_back
    .org kvm_ia64_ivt+0x6100
/////////////////////////////////////////////////////////////////////
// 0x6100 Entry 37 (size 16 bundles) Virtualization Fault
ENTRY(kvm_virtualization_fault)
	mov r31=pr
	adds r16 = VMM_VCPU_SAVED_GP_OFFSET,r21
	;;
	st8 [r16] = r1
	adds r17 = VMM_VCPU_GP_OFFSET, r21
	;;
	ld8 r1 = [r17]
	cmp.eq p6,p0=EVENT_MOV_FROM_AR,r24
	cmp.eq p7,p0=EVENT_MOV_FROM_RR,r24
	cmp.eq p8,p0=EVENT_MOV_TO_RR,r24
	cmp.eq p9,p0=EVENT_RSM,r24
	cmp.eq p10,p0=EVENT_SSM,r24
	cmp.eq p11,p0=EVENT_MOV_TO_PSR,r24
	cmp.eq p12,p0=EVENT_THASH,r24
(p6)	br.dptk.many kvm_asm_mov_from_ar
(p7)	br.dptk.many kvm_asm_mov_from_rr
(p8)	br.dptk.many kvm_asm_mov_to_rr
(p9)	br.dptk.many kvm_asm_rsm
(p10)	br.dptk.many kvm_asm_ssm
(p11)	br.dptk.many kvm_asm_mov_to_psr
(p12)	br.dptk.many kvm_asm_thash
	;;
kvm_virtualization_fault_back:
	adds r16 = VMM_VCPU_SAVED_GP_OFFSET,r21
	;;
	ld8 r1 = [r16]
	;;
	mov r19=37
	adds r16 = VMM_VCPU_CAUSE_OFFSET,r21
	adds r17 = VMM_VCPU_OPCODE_OFFSET,r21
	;;
	st8 [r16] = r24
	st8 [r17] = r25
	;;
	cmp.ne p6,p0=EVENT_RFI, r24
(p6)	br.sptk kvm_dispatch_virtualization_fault
	;;
	adds r18=VMM_VPD_BASE_OFFSET,r21
	;;
	ld8 r18=[r18]
	;;
	adds r18=VMM_VPD_VIFS_OFFSET,r18
	;;
	ld8 r18=[r18]
	;;
	tbit.z p6,p0=r18,63
(p6)	br.sptk kvm_dispatch_virtualization_fault
	;;
//if vifs.v=1 desert current register frame
	alloc r18=ar.pfs,0,0,0,0
	br.sptk kvm_dispatch_virtualization_fault
END(kvm_virtualization_fault)

    .org kvm_ia64_ivt+0x6200
//////////////////////////////////////////////////////////////
// 0x6200 Entry 38 (size 16 bundles) Reserved
	KVM_FAULT(38)

    .org kvm_ia64_ivt+0x6300
/////////////////////////////////////////////////////////////////
// 0x6300 Entry 39 (size 16 bundles) Reserved
	KVM_FAULT(39)

    .org kvm_ia64_ivt+0x6400
/////////////////////////////////////////////////////////////////
// 0x6400 Entry 40 (size 16 bundles) Reserved
	KVM_FAULT(40)

    .org kvm_ia64_ivt+0x6500
//////////////////////////////////////////////////////////////////
// 0x6500 Entry 41 (size 16 bundles) Reserved
	KVM_FAULT(41)

    .org kvm_ia64_ivt+0x6600
//////////////////////////////////////////////////////////////////
// 0x6600 Entry 42 (size 16 bundles) Reserved
	KVM_FAULT(42)

    .org kvm_ia64_ivt+0x6700
//////////////////////////////////////////////////////////////////
// 0x6700 Entry 43 (size 16 bundles) Reserved
	KVM_FAULT(43)

    .org kvm_ia64_ivt+0x6800
//////////////////////////////////////////////////////////////////
// 0x6800 Entry 44 (size 16 bundles) Reserved
	KVM_FAULT(44)

    .org kvm_ia64_ivt+0x6900
///////////////////////////////////////////////////////////////////
// 0x6900 Entry 45 (size 16 bundles) IA-32 Exeception
//(17,18,29,41,42,43,44,58,60,61,62,72,73,75,76,77)
ENTRY(kvm_ia32_exception)
	KVM_FAULT(45)
END(kvm_ia32_exception)

    .org kvm_ia64_ivt+0x6a00
////////////////////////////////////////////////////////////////////
// 0x6a00 Entry 46 (size 16 bundles) IA-32 Intercept  (30,31,59,70,71)
ENTRY(kvm_ia32_intercept)
	KVM_FAULT(47)
END(kvm_ia32_intercept)

    .org kvm_ia64_ivt+0x6c00
/////////////////////////////////////////////////////////////////////
// 0x6c00 Entry 48 (size 16 bundles) Reserved
	KVM_FAULT(48)

    .org kvm_ia64_ivt+0x6d00
//////////////////////////////////////////////////////////////////////
// 0x6d00 Entry 49 (size 16 bundles) Reserved
	KVM_FAULT(49)

    .org kvm_ia64_ivt+0x6e00
//////////////////////////////////////////////////////////////////////
// 0x6e00 Entry 50 (size 16 bundles) Reserved
	KVM_FAULT(50)

    .org kvm_ia64_ivt+0x6f00
/////////////////////////////////////////////////////////////////////
// 0x6f00 Entry 51 (size 16 bundles) Reserved
	KVM_FAULT(52)

    .org kvm_ia64_ivt+0x7100
////////////////////////////////////////////////////////////////////
// 0x7100 Entry 53 (size 16 bundles) Reserved
	KVM_FAULT(53)

    .org kvm_ia64_ivt+0x7200
/////////////////////////////////////////////////////////////////////
// 0x7200 Entry 54 (size 16 bundles) Reserved
	KVM_FAULT(54)

    .org kvm_ia64_ivt+0x7300
////////////////////////////////////////////////////////////////////
// 0x7300 Entry 55 (size 16 bundles) Reserved
	KVM_FAULT(55)

    .org kvm_ia64_ivt+0x7400
////////////////////////////////////////////////////////////////////
// 0x7400 Entry 56 (size 16 bundles) Reserved
	KVM_FAULT(56)

    .org kvm_ia64_ivt+0x7500
/////////////////////////////////////////////////////////////////////
// 0x7500 Entry 57 (size 16 bundles) Reserved
	KVM_FAULT(57)

    .org kvm_ia64_ivt+0x7600
/////////////////////////////////////////////////////////////////////
// 0x7600 Entry 58 (size 16 bundles) Reserved
	KVM_FAULT(58)

    .org kvm_ia64_ivt+0x7700
////////////////////////////////////////////////////////////////////
// 0x7700 Entry 59 (size 16 bundles) Reserved
	KVM_FAULT(59)

    .org kvm_ia64_ivt+0x7800
////////////////////////////////////////////////////////////////////
// 0x7800 Entry 60 (size 16 bundles) Reserved
	KVM_FAULT(60)

    .org kvm_ia64_ivt+0x7900
/////////////////////////////////////////////////////////////////////
// 0x7900 Entry 61 (size 16 bundles) Reserved
	KVM_FAULT(61)

    .org kvm_ia64_ivt+0x7a00
/////////////////////////////////////////////////////////////////////
// 0x7a00 Entry 62 (size 16 bundles) Reserved
	KVM_FAULT(62)

    .org kvm_ia64_ivt+0x7b00
/////////////////////////////////////////////////////////////////////
// 0x7b00 Entry 63 (size 16 bundles) Reserved
	KVM_FAULT(63)

    .org kvm_ia64_ivt+0x7c00
////////////////////////////////////////////////////////////////////
// 0x7c00 Entry 64 (size 16 bundles) Reserved
	KVM_FAULT(64)

    .org kvm_ia64_ivt+0x7d00
/////////////////////////////////////////////////////////////////////
// 0x7d00 Entry 65 (size 16 bundles) Reserved
	KVM_FAULT(65)

    .org kvm_ia64_ivt+0x7e00
/////////////////////////////////////////////////////////////////////
// 0x7e00 Entry 66 (size 16 bundles) Reserved
	KVM_FAULT(66)

    .org kvm_ia64_ivt+0x7f00
////////////////////////////////////////////////////////////////////
// 0x7f00 Entry 67 (size 16 bundles) Reserved
	KVM_FAULT(67)

    .org kvm_ia64_ivt+0x8000
// There is no particular reason for this code to be here, other than that
// there happens to be space here that would go unused otherwise.  If this
// fault ever gets "unreserved", simply moved the following code to a more
// suitable spot...


ENTRY(kvm_dtlb_miss_dispatch)
	mov r19 = 2
	KVM_SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,3,0
	mov out0=cr.ifa
	mov out1=r15
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i     // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.i
	addl r14=@gprel(ia64_leave_hypervisor_prepare),gp
	;;
	KVM_SAVE_REST
	KVM_SAVE_EXTRA
	mov rp=r14
	;;
	adds out2=16,r12
	br.call.sptk.many b6=kvm_page_fault
END(kvm_dtlb_miss_dispatch)

ENTRY(kvm_itlb_miss_dispatch)

	KVM_SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,3,0
	mov out0=cr.ifa
	mov out1=r15
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i   // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.i
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	KVM_SAVE_REST
	mov rp=r14
	;;
	adds out2=16,r12
	br.call.sptk.many b6=kvm_page_fault
END(kvm_itlb_miss_dispatch)

ENTRY(kvm_dispatch_reflection)
/*
 * Input:
 *  psr.ic: off
 *  r19:    intr type (offset into ivt, see ia64_int.h)
 *  r31:    contains saved predicates (pr)
 */
	KVM_SAVE_MIN_WITH_COVER_R19
	alloc r14=ar.pfs,0,0,5,0
	mov out0=cr.ifa
	mov out1=cr.isr
	mov out2=cr.iim
	mov out3=r15
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i   // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.i
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	KVM_SAVE_REST
	mov rp=r14
	;;
	adds out4=16,r12
	br.call.sptk.many b6=reflect_interruption
END(kvm_dispatch_reflection)

ENTRY(kvm_dispatch_virtualization_fault)
	adds r16 = VMM_VCPU_CAUSE_OFFSET,r21
	adds r17 = VMM_VCPU_OPCODE_OFFSET,r21
	;;
	st8 [r16] = r24
	st8 [r17] = r25
	;;
	KVM_SAVE_MIN_WITH_COVER_R19
	;;
	alloc r14=ar.pfs,0,0,2,0 // (must be first in insn group!)
	mov out0=r13        //vcpu
	adds r3=8,r2                // set up second base pointer
	;;
	ssm psr.ic
	;;
	srlz.i    // guarantee that interruption collection is on
	;;
	(p15) ssm psr.i               // restore psr.i
	addl r14=@gprel(ia64_leave_hypervisor_prepare),gp
	;;
	KVM_SAVE_REST
	KVM_SAVE_EXTRA
	mov rp=r14
	;;
	adds out1=16,sp         //regs
	br.call.sptk.many b6=kvm_emulate
END(kvm_dispatch_virtualization_fault)


ENTRY(kvm_dispatch_interrupt)
	KVM_SAVE_MIN_WITH_COVER_R19	// uses r31; defines r2 and r3
	;;
	alloc r14=ar.pfs,0,0,1,0 // must be first in an insn group
	adds r3=8,r2		// set up second base pointer for SAVE_REST
	;;
	ssm psr.ic
	;;
	srlz.i
	;;
	(p15) ssm psr.i
	addl r14=@gprel(ia64_leave_hypervisor),gp
	;;
	KVM_SAVE_REST
	mov rp=r14
	;;
	mov out0=r13		// pass pointer to pt_regs as second arg
	br.call.sptk.many b6=kvm_ia64_handle_irq
END(kvm_dispatch_interrupt)

GLOBAL_ENTRY(ia64_leave_nested)
	rsm psr.i
	;;
	adds r21=PT(PR)+16,r12
	;;
	lfetch [r21],PT(CR_IPSR)-PT(PR)
	adds r2=PT(B6)+16,r12
	adds r3=PT(R16)+16,r12
	;;
	lfetch [r21]
	ld8 r28=[r2],8		// load b6
	adds r29=PT(R24)+16,r12

	ld8.fill r16=[r3]
	adds r3=PT(AR_CSD)-PT(R16),r3
	adds r30=PT(AR_CCV)+16,r12
	;;
	ld8.fill r24=[r29]
	ld8 r15=[r30]		// load ar.ccv
	;;
	ld8 r29=[r2],16		// load b7
	ld8 r30=[r3],16		// load ar.csd
	;;
	ld8 r31=[r2],16		// load ar.ssd
	ld8.fill r8=[r3],16
	;;
	ld8.fill r9=[r2],16
	ld8.fill r10=[r3],PT(R17)-PT(R10)
	;;
	ld8.fill r11=[r2],PT(R18)-PT(R11)
	ld8.fill r17=[r3],16
	;;
	ld8.fill r18=[r2],16
	ld8.fill r19=[r3],16
	;;
	ld8.fill r20=[r2],16
	ld8.fill r21=[r3],16
	mov ar.csd=r30
	mov ar.ssd=r31
	;;
	rsm psr.i | psr.ic
	// initiate turning off of interrupt and interruption collection
	invala			// invalidate ALAT
	;;
	srlz.i
	;;
	ld8.fill r22=[r2],24
	ld8.fill r23=[r3],24
	mov b6=r28
	;;
	ld8.fill r25=[r2],16
	ld8.fill r26=[r3],16
	mov b7=r29
	;;
	ld8.fill r27=[r2],16
	ld8.fill r28=[r3],16
	;;
	ld8.fill r29=[r2],16
	ld8.fill r30=[r3],24
	;;
	ld8.fill r31=[r2],PT(F9)-PT(R31)
	adds r3=PT(F10)-PT(F6),r3
	;;
	ldf.fill f9=[r2],PT(F6)-PT(F9)
	ldf.fill f10=[r3],PT(F8)-PT(F10)
	;;
	ldf.fill f6=[r2],PT(F7)-PT(F6)
	;;
	ldf.fill f7=[r2],PT(F11)-PT(F7)
	ldf.fill f8=[r3],32
	;;
	srlz.i			// ensure interruption collection is off
	mov ar.ccv=r15
	;;
	bsw.0	// switch back to bank 0 (no stop bit required beforehand...)
	;;
	ldf.fill f11=[r2]
//	mov r18=r13
//	mov r21=r13
	adds r16=PT(CR_IPSR)+16,r12
	adds r17=PT(CR_IIP)+16,r12
	;;
	ld8 r29=[r16],16	// load cr.ipsr
	ld8 r28=[r17],16	// load cr.iip
	;;
	ld8 r30=[r16],16	// load cr.ifs
	ld8 r25=[r17],16	// load ar.unat
	;;
	ld8 r26=[r16],16	// load ar.pfs
	ld8 r27=[r17],16	// load ar.rsc
	cmp.eq p9,p0=r0,r0
	// set p9 to indicate that we should restore cr.ifs
	;;
	ld8 r24=[r16],16	// load ar.rnat (may be garbage)
	ld8 r23=[r17],16// load ar.bspstore (may be garbage)
	;;
	ld8 r31=[r16],16	// load predicates
	ld8 r22=[r17],16	// load b0
	;;
	ld8 r19=[r16],16	// load ar.rsc value for "loadrs"
	ld8.fill r1=[r17],16	// load r1
	;;
	ld8.fill r12=[r16],16
	ld8.fill r13=[r17],16
	;;
	ld8 r20=[r16],16	// ar.fpsr
	ld8.fill r15=[r17],16
	;;
	ld8.fill r14=[r16],16
	ld8.fill r2=[r17]
	;;
	ld8.fill r3=[r16]
	;;
	mov r16=ar.bsp		// get existing backing store pointer
	;;
	mov b0=r22
	mov ar.pfs=r26
	mov cr.ifs=r30
	mov cr.ipsr=r29
	mov ar.fpsr=r20
	mov cr.iip=r28
	;;
	mov ar.rsc=r27
	mov ar.unat=r25
	mov pr=r31,-1
	rfi
END(ia64_leave_nested)

GLOBAL_ENTRY(ia64_leave_hypervisor_prepare)
/*
 * work.need_resched etc. mustn't get changed
 *by this CPU before it returns to
 * user- or fsys-mode, hence we disable interrupts early on:
 */
	adds r2 = PT(R4)+16,r12
	adds r3 = PT(R5)+16,r12
	adds r8 = PT(EML_UNAT)+16,r12
	;;
	ld8 r8 = [r8]
	;;
	mov ar.unat=r8
	;;
	ld8.fill r4=[r2],16    //load r4
	ld8.fill r5=[r3],16    //load r5
	;;
	ld8.fill r6=[r2]    //load r6
	ld8.fill r7=[r3]    //load r7
	;;
END(ia64_leave_hypervisor_prepare)
//fall through
GLOBAL_ENTRY(ia64_leave_hypervisor)
	rsm psr.i
	;;
	br.call.sptk.many b0=leave_hypervisor_tail
	;;
	adds r20=PT(PR)+16,r12
	adds r8=PT(EML_UNAT)+16,r12
	;;
	ld8 r8=[r8]
	;;
	mov ar.unat=r8
	;;
	lfetch [r20],PT(CR_IPSR)-PT(PR)
	adds r2 = PT(B6)+16,r12
	adds r3 = PT(B7)+16,r12
	;;
	lfetch [r20]
	;;
	ld8 r24=[r2],16        /* B6 */
	ld8 r25=[r3],16        /* B7 */
	;;
	ld8 r26=[r2],16        /* ar_csd */
	ld8 r27=[r3],16        /* ar_ssd */
	mov b6 = r24
	;;
	ld8.fill r8=[r2],16
	ld8.fill r9=[r3],16
	mov b7 = r25
	;;
	mov ar.csd = r26
	mov ar.ssd = r27
	;;
	ld8.fill r10=[r2],PT(R15)-PT(R10)
	ld8.fill r11=[r3],PT(R14)-PT(R11)
	;;
	ld8.fill r15=[r2],PT(R16)-PT(R15)
	ld8.fill r14=[r3],PT(R17)-PT(R14)
	;;
	ld8.fill r16=[r2],16
	ld8.fill r17=[r3],16
	;;
	ld8.fill r18=[r2],16
	ld8.fill r19=[r3],16
	;;
	ld8.fill r20=[r2],16
	ld8.fill r21=[r3],16
	;;
	ld8.fill r22=[r2],16
	ld8.fill r23=[r3],16
	;;
	ld8.fill r24=[r2],16
	ld8.fill r25=[r3],16
	;;
	ld8.fill r26=[r2],16
	ld8.fill r27=[r3],16
	;;
	ld8.fill r28=[r2],16
	ld8.fill r29=[r3],16
	;;
	ld8.fill r30=[r2],PT(F6)-PT(R30)
	ld8.fill r31=[r3],PT(F7)-PT(R31)
	;;
	rsm psr.i | psr.ic
	// initiate turning off of interrupt and interruption collection
	invala          // invalidate ALAT
	;;
	srlz.i          // ensure interruption collection is off
	;;
	bsw.0
	;;
	adds r16 = PT(CR_IPSR)+16,r12
	adds r17 = PT(CR_IIP)+16,r12
	mov r21=r13		// get current
	;;
	ld8 r31=[r16],16    // load cr.ipsr
	ld8 r30=[r17],16    // load cr.iip
	;;
	ld8 r29=[r16],16    // load cr.ifs
	ld8 r28=[r17],16    // load ar.unat
	;;
	ld8 r27=[r16],16    // load ar.pfs
	ld8 r26=[r17],16    // load ar.rsc
	;;
	ld8 r25=[r16],16    // load ar.rnat
	ld8 r24=[r17],16    // load ar.bspstore
	;;
	ld8 r23=[r16],16    // load predicates
	ld8 r22=[r17],16    // load b0
	;;
	ld8 r20=[r16],16    // load ar.rsc value for "loadrs"
	ld8.fill r1=[r17],16    //load r1
	;;
	ld8.fill r12=[r16],16    //load r12
	ld8.fill r13=[r17],PT(R2)-PT(R13)    //load r13
	;;
	ld8 r19=[r16],PT(R3)-PT(AR_FPSR)    //load ar_fpsr
	ld8.fill r2=[r17],PT(AR_CCV)-PT(R2)    //load r2
	;;
	ld8.fill r3=[r16]	//load r3
	ld8 r18=[r17]	//load ar_ccv
	;;
	mov ar.fpsr=r19
	mov ar.ccv=r18
	shr.u r18=r20,16
	;;
kvm_rbs_switch:
	mov r19=96

kvm_dont_preserve_current_frame:
/*
    * To prevent leaking bits between the hypervisor and guest domain,
    * we must clear the stacked registers in the "invalid" partition here.
    * 5 registers/cycle on McKinley).
    */
#   define pRecurse	p6
#   define pReturn	p7
#   define Nregs	14

	alloc loc0=ar.pfs,2,Nregs-2,2,0
	shr.u loc1=r18,9	// RNaTslots <= floor(dirtySize / (64*8))
	sub r19=r19,r18		// r19 = (physStackedSize + 8) - dirtySize
	;;
	mov ar.rsc=r20		// load ar.rsc to be used for "loadrs"
	shladd in0=loc1,3,r19
	mov in1=0
	;;
	TEXT_ALIGN(32)
kvm_rse_clear_invalid:
	alloc loc0=ar.pfs,2,Nregs-2,2,0
	cmp.lt pRecurse,p0=Nregs*8,in0
	// if more than Nregs regs left to clear, (re)curse
	add out0=-Nregs*8,in0
	add out1=1,in1		// increment recursion count
	mov loc1=0
	mov loc2=0
	;;
	mov loc3=0
	mov loc4=0
	mov loc5=0
	mov loc6=0
	mov loc7=0
(pRecurse) br.call.dptk.few b0=kvm_rse_clear_invalid
	;;
	mov loc8=0
	mov loc9=0
	cmp.ne pReturn,p0=r0,in1
	// if recursion count != 0, we need to do a br.ret
	mov loc10=0
	mov loc11=0
(pReturn) br.ret.dptk.many b0

#	undef pRecurse
#	undef pReturn

// loadrs has already been shifted
	alloc r16=ar.pfs,0,0,0,0    // drop current register frame
	;;
	loadrs
	;;
	mov ar.bspstore=r24
	;;
	mov ar.unat=r28
	mov ar.rnat=r25
	mov ar.rsc=r26
	;;
	mov cr.ipsr=r31
	mov cr.iip=r30
	mov cr.ifs=r29
	mov ar.pfs=r27
	adds r18=VMM_VPD_BASE_OFFSET,r21
	;;
	ld8 r18=[r18]   //vpd
	adds r17=VMM_VCPU_ISR_OFFSET,r21
	;;
	ld8 r17=[r17]
	adds r19=VMM_VPD_VPSR_OFFSET,r18
	;;
	ld8 r19=[r19]        //vpsr
	mov r25=r18
	adds r16= VMM_VCPU_GP_OFFSET,r21
	;;
	ld8 r16= [r16] // Put gp in r24
	movl r24=@gprel(ia64_vmm_entry)  // calculate return address
	;;
	add  r24=r24,r16
	;;
	br.sptk.many  kvm_vps_sync_write       // call the service
	;;
END(ia64_leave_hypervisor)
// fall through
GLOBAL_ENTRY(ia64_vmm_entry)
/*
 *  must be at bank 0
 *  parameter:
 *  r17:cr.isr
 *  r18:vpd
 *  r19:vpsr
 *  r22:b0
 *  r23:predicate
 */
	mov r24=r22
	mov r25=r18
	tbit.nz p1,p2 = r19,IA64_PSR_IC_BIT        // p1=vpsr.ic
(p1) 	br.cond.sptk.few kvm_vps_resume_normal
(p2)	br.cond.sptk.many kvm_vps_resume_handler
	;;
END(ia64_vmm_entry)

/*
 * extern u64 ia64_call_vsa(u64 proc, u64 arg1, u64 arg2,
 *                  u64 arg3, u64 arg4, u64 arg5,
 *                  u64 arg6, u64 arg7);
 *
 * XXX: The currently defined services use only 4 args at the max. The
 *  rest are not consumed.
 */
GLOBAL_ENTRY(ia64_call_vsa)
    .regstk 4,4,0,0

rpsave  =   loc0
pfssave =   loc1
psrsave =   loc2
entry   =   loc3
hostret =   r24

	alloc   pfssave=ar.pfs,4,4,0,0
	mov rpsave=rp
	adds entry=VMM_VCPU_VSA_BASE_OFFSET, r13
	;;
	ld8 entry=[entry]
1:	mov hostret=ip
	mov r25=in1         // copy arguments
	mov r26=in2
	mov r27=in3
	mov psrsave=psr
	;;
	tbit.nz p6,p0=psrsave,14    // IA64_PSR_I
	tbit.nz p7,p0=psrsave,13    // IA64_PSR_IC
	;;
	add hostret=2f-1b,hostret   // calculate return address
	add entry=entry,in0
	;;
	rsm psr.i | psr.ic
	;;
	srlz.i
	mov b6=entry
	br.cond.sptk b6         // call the service
2:
// Architectural sequence for enabling interrupts if necessary
(p7)    ssm psr.ic
	;;
(p7)    srlz.i
	;;
(p6)    ssm psr.i
	;;
	mov rp=rpsave
	mov ar.pfs=pfssave
	mov r8=r31
	;;
	srlz.d
	br.ret.sptk rp

END(ia64_call_vsa)

#define  INIT_BSPSTORE  ((4<<30)-(12<<20)-0x100)

GLOBAL_ENTRY(vmm_reset_entry)
	//set up ipsr, iip, vpd.vpsr, dcr
	// For IPSR: it/dt/rt=1, i/ic=1, si=1, vm/bn=1
	// For DCR: all bits 0
	bsw.0
	;;
	mov r21 =r13
	adds r14=-VMM_PT_REGS_SIZE, r12
	;;
	movl r6=0x501008826000      // IPSR dt/rt/it:1;i/ic:1, si:1, vm/bn:1
	movl r10=0x8000000000000000
	adds r16=PT(CR_IIP), r14
	adds r20=PT(R1), r14
	;;
	rsm psr.ic | psr.i
	;;
	srlz.i
	;;
	mov ar.rsc = 0
	;;
	flushrs
	;;
	mov ar.bspstore = 0
	// clear BSPSTORE
	;;
	mov cr.ipsr=r6
	mov cr.ifs=r10
	ld8 r4 = [r16] // Set init iip for first run.
	ld8 r1 = [r20]
	;;
	mov cr.iip=r4
	adds r16=VMM_VPD_BASE_OFFSET,r13
	;;
	ld8 r18=[r16]
	;;
	adds r19=VMM_VPD_VPSR_OFFSET,r18
	;;
	ld8 r19=[r19]
	mov r17=r0
	mov r22=r0
	mov r23=r0
	br.cond.sptk ia64_vmm_entry
	br.ret.sptk  b0
END(vmm_reset_entry)
