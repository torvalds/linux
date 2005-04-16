//
// assembly portion of the IA64 MCA handling
//
// Mods by cfleck to integrate into kernel build
// 00/03/15 davidm Added various stop bits to get a clean compile
//
// 00/03/29 cfleck Added code to save INIT handoff state in pt_regs format, switch to temp
//		   kstack, switch modes, jump to C INIT handler
//
// 02/01/04 J.Hall <jenna.s.hall@intel.com>
//		   Before entering virtual mode code:
//		   1. Check for TLB CPU error
//		   2. Restore current thread pointer to kr6
//		   3. Move stack ptr 16 bytes to conform to C calling convention
//
// 04/11/12 Russ Anderson <rja@sgi.com>
//		   Added per cpu MCA/INIT stack save areas.
//
#include <linux/config.h>
#include <linux/threads.h>

#include <asm/asmmacro.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mca_asm.h>
#include <asm/mca.h>

/*
 * When we get a machine check, the kernel stack pointer is no longer
 * valid, so we need to set a new stack pointer.
 */
#define	MINSTATE_PHYS	/* Make sure stack access is physical for MINSTATE */

/*
 * Needed for return context to SAL
 */
#define IA64_MCA_SAME_CONTEXT	0
#define IA64_MCA_COLD_BOOT	-2

#include "minstate.h"

/*
 * SAL_TO_OS_MCA_HANDOFF_STATE (SAL 3.0 spec)
 *		1. GR1 = OS GP
 *		2. GR8 = PAL_PROC physical address
 *		3. GR9 = SAL_PROC physical address
 *		4. GR10 = SAL GP (physical)
 *		5. GR11 = Rendez state
 *		6. GR12 = Return address to location within SAL_CHECK
 */
#define SAL_TO_OS_MCA_HANDOFF_STATE_SAVE(_tmp)		\
	LOAD_PHYSICAL(p0, _tmp, ia64_sal_to_os_handoff_state);; \
	st8	[_tmp]=r1,0x08;;			\
	st8	[_tmp]=r8,0x08;;			\
	st8	[_tmp]=r9,0x08;;			\
	st8	[_tmp]=r10,0x08;;			\
	st8	[_tmp]=r11,0x08;;			\
	st8	[_tmp]=r12,0x08;;			\
	st8	[_tmp]=r17,0x08;;			\
	st8	[_tmp]=r18,0x08

/*
 * OS_MCA_TO_SAL_HANDOFF_STATE (SAL 3.0 spec)
 * (p6) is executed if we never entered virtual mode (TLB error)
 * (p7) is executed if we entered virtual mode as expected (normal case)
 *	1. GR8 = OS_MCA return status
 *	2. GR9 = SAL GP (physical)
 *	3. GR10 = 0/1 returning same/new context
 *	4. GR22 = New min state save area pointer
 *	returns ptr to SAL rtn save loc in _tmp
 */
#define OS_MCA_TO_SAL_HANDOFF_STATE_RESTORE(_tmp)	\
	movl	_tmp=ia64_os_to_sal_handoff_state;;	\
	DATA_VA_TO_PA(_tmp);;				\
	ld8	r8=[_tmp],0x08;;			\
	ld8	r9=[_tmp],0x08;;			\
	ld8	r10=[_tmp],0x08;;			\
	ld8	r22=[_tmp],0x08;;
	// now _tmp is pointing to SAL rtn save location

/*
 * COLD_BOOT_HANDOFF_STATE() sets ia64_mca_os_to_sal_state
 *	imots_os_status=IA64_MCA_COLD_BOOT
 *	imots_sal_gp=SAL GP
 *	imots_context=IA64_MCA_SAME_CONTEXT
 *	imots_new_min_state=Min state save area pointer
 *	imots_sal_check_ra=Return address to location within SAL_CHECK
 *
 */
#define COLD_BOOT_HANDOFF_STATE(sal_to_os_handoff,os_to_sal_handoff,tmp)\
	movl	tmp=IA64_MCA_COLD_BOOT;					\
	movl	sal_to_os_handoff=__pa(ia64_sal_to_os_handoff_state);	\
	movl	os_to_sal_handoff=__pa(ia64_os_to_sal_handoff_state);;	\
	st8	[os_to_sal_handoff]=tmp,8;;				\
	ld8	tmp=[sal_to_os_handoff],48;;				\
	st8	[os_to_sal_handoff]=tmp,8;;				\
	movl	tmp=IA64_MCA_SAME_CONTEXT;;				\
	st8	[os_to_sal_handoff]=tmp,8;;				\
	ld8	tmp=[sal_to_os_handoff],-8;;				\
	st8     [os_to_sal_handoff]=tmp,8;;				\
	ld8	tmp=[sal_to_os_handoff];;				\
	st8     [os_to_sal_handoff]=tmp;;

#define GET_IA64_MCA_DATA(reg)						\
	GET_THIS_PADDR(reg, ia64_mca_data)				\
	;;								\
	ld8 reg=[reg]

	.global ia64_os_mca_dispatch
	.global ia64_os_mca_dispatch_end
	.global ia64_sal_to_os_handoff_state
	.global	ia64_os_to_sal_handoff_state

	.text
	.align 16

ia64_os_mca_dispatch:

	// Serialize all MCA processing
	mov	r3=1;;
	LOAD_PHYSICAL(p0,r2,ia64_mca_serialize);;
ia64_os_mca_spin:
	xchg8	r4=[r2],r3;;
	cmp.ne	p6,p0=r4,r0
(p6)	br ia64_os_mca_spin

	// Save the SAL to OS MCA handoff state as defined
	// by SAL SPEC 3.0
	// NOTE : The order in which the state gets saved
	//	  is dependent on the way the C-structure
	//	  for ia64_mca_sal_to_os_state_t has been
	//	  defined in include/asm/mca.h
	SAL_TO_OS_MCA_HANDOFF_STATE_SAVE(r2)
	;;

	// LOG PROCESSOR STATE INFO FROM HERE ON..
begin_os_mca_dump:
	br	ia64_os_mca_proc_state_dump;;

ia64_os_mca_done_dump:

	LOAD_PHYSICAL(p0,r16,ia64_sal_to_os_handoff_state+56)
	;;
	ld8 r18=[r16]		// Get processor state parameter on existing PALE_CHECK.
	;;
	tbit.nz p6,p7=r18,60
(p7)	br.spnt done_tlb_purge_and_reload

	// The following code purges TC and TR entries. Then reload all TC entries.
	// Purge percpu data TC entries.
begin_tlb_purge_and_reload:

#define O(member)	IA64_CPUINFO_##member##_OFFSET

	GET_THIS_PADDR(r2, cpu_info)	// load phys addr of cpu_info into r2
	;;
	addl r17=O(PTCE_STRIDE),r2
	addl r2=O(PTCE_BASE),r2
	;;
	ld8 r18=[r2],(O(PTCE_COUNT)-O(PTCE_BASE));;	// r18=ptce_base
	ld4 r19=[r2],4					// r19=ptce_count[0]
	ld4 r21=[r17],4					// r21=ptce_stride[0]
	;;
	ld4 r20=[r2]					// r20=ptce_count[1]
	ld4 r22=[r17]					// r22=ptce_stride[1]
	mov r24=0
	;;
	adds r20=-1,r20
	;;
#undef O

2:
	cmp.ltu p6,p7=r24,r19
(p7)	br.cond.dpnt.few 4f
	mov ar.lc=r20
3:
	ptc.e r18
	;;
	add r18=r22,r18
	br.cloop.sptk.few 3b
	;;
	add r18=r21,r18
	add r24=1,r24
	;;
	br.sptk.few 2b
4:
	srlz.i 			// srlz.i implies srlz.d
	;;

        // Now purge addresses formerly mapped by TR registers
	// 1. Purge ITR&DTR for kernel.
	movl r16=KERNEL_START
	mov r18=KERNEL_TR_PAGE_SHIFT<<2
	;;
	ptr.i r16, r18
	ptr.d r16, r18
	;;
	srlz.i
	;;
	srlz.d
	;;
	// 2. Purge DTR for PERCPU data.
	movl r16=PERCPU_ADDR
	mov r18=PERCPU_PAGE_SHIFT<<2
	;;
	ptr.d r16,r18
	;;
	srlz.d
	;;
	// 3. Purge ITR for PAL code.
	GET_THIS_PADDR(r2, ia64_mca_pal_base)
	;;
	ld8 r16=[r2]
	mov r18=IA64_GRANULE_SHIFT<<2
	;;
	ptr.i r16,r18
	;;
	srlz.i
	;;
	// 4. Purge DTR for stack.
	mov r16=IA64_KR(CURRENT_STACK)
	;;
	shl r16=r16,IA64_GRANULE_SHIFT
	movl r19=PAGE_OFFSET
	;;
	add r16=r19,r16
	mov r18=IA64_GRANULE_SHIFT<<2
	;;
	ptr.d r16,r18
	;;
	srlz.i
	;;
	// Finally reload the TR registers.
	// 1. Reload DTR/ITR registers for kernel.
	mov r18=KERNEL_TR_PAGE_SHIFT<<2
	movl r17=KERNEL_START
	;;
	mov cr.itir=r18
	mov cr.ifa=r17
        mov r16=IA64_TR_KERNEL
	mov r19=ip
	movl r18=PAGE_KERNEL
	;;
        dep r17=0,r19,0, KERNEL_TR_PAGE_SHIFT
	;;
	or r18=r17,r18
	;;
        itr.i itr[r16]=r18
	;;
        itr.d dtr[r16]=r18
        ;;
	srlz.i
	srlz.d
	;;
	// 2. Reload DTR register for PERCPU data.
	GET_THIS_PADDR(r2, ia64_mca_per_cpu_pte)
	;;
	movl r16=PERCPU_ADDR		// vaddr
	movl r18=PERCPU_PAGE_SHIFT<<2
	;;
	mov cr.itir=r18
	mov cr.ifa=r16
	;;
	ld8 r18=[r2]			// load per-CPU PTE
	mov r16=IA64_TR_PERCPU_DATA;
	;;
	itr.d dtr[r16]=r18
	;;
	srlz.d
	;;
	// 3. Reload ITR for PAL code.
	GET_THIS_PADDR(r2, ia64_mca_pal_pte)
	;;
	ld8 r18=[r2]			// load PAL PTE
	;;
	GET_THIS_PADDR(r2, ia64_mca_pal_base)
	;;
	ld8 r16=[r2]			// load PAL vaddr
	mov r19=IA64_GRANULE_SHIFT<<2
	;;
	mov cr.itir=r19
	mov cr.ifa=r16
	mov r20=IA64_TR_PALCODE
	;;
	itr.i itr[r20]=r18
	;;
	srlz.i
	;;
	// 4. Reload DTR for stack.
	mov r16=IA64_KR(CURRENT_STACK)
	;;
	shl r16=r16,IA64_GRANULE_SHIFT
	movl r19=PAGE_OFFSET
	;;
	add r18=r19,r16
	movl r20=PAGE_KERNEL
	;;
	add r16=r20,r16
	mov r19=IA64_GRANULE_SHIFT<<2
	;;
	mov cr.itir=r19
	mov cr.ifa=r18
	mov r20=IA64_TR_CURRENT_STACK
	;;
	itr.d dtr[r20]=r16
	;;
	srlz.d
	;;
	br.sptk.many done_tlb_purge_and_reload
err:
	COLD_BOOT_HANDOFF_STATE(r20,r21,r22)
	br.sptk.many ia64_os_mca_done_restore

done_tlb_purge_and_reload:

	// Setup new stack frame for OS_MCA handling
	GET_IA64_MCA_DATA(r2)
	;;
	add r3 = IA64_MCA_CPU_STACKFRAME_OFFSET, r2
	add r2 = IA64_MCA_CPU_RBSTORE_OFFSET, r2
	;;
	rse_switch_context(r6,r3,r2);;	// RSC management in this new context

	GET_IA64_MCA_DATA(r2)
	;;
	add r2 = IA64_MCA_CPU_STACK_OFFSET+IA64_MCA_STACK_SIZE-16, r2
	;;
	mov r12=r2		// establish new stack-pointer

        // Enter virtual mode from physical mode
	VIRTUAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_begin, r4)
ia64_os_mca_virtual_begin:

	// Call virtual mode handler
	movl		r2=ia64_mca_ucmc_handler;;
	mov		b6=r2;;
	br.call.sptk.many    b0=b6;;
.ret0:
	// Revert back to physical mode before going back to SAL
	PHYSICAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_end, r4)
ia64_os_mca_virtual_end:

	// restore the original stack frame here
	GET_IA64_MCA_DATA(r2)
	;;
	add r2 = IA64_MCA_CPU_STACKFRAME_OFFSET, r2
	;;
	movl    r4=IA64_PSR_MC
	;;
	rse_return_context(r4,r3,r2)	// switch from interrupt context for RSE

	// let us restore all the registers from our PSI structure
	mov	r8=gp
	;;
begin_os_mca_restore:
	br	ia64_os_mca_proc_state_restore;;

ia64_os_mca_done_restore:
	OS_MCA_TO_SAL_HANDOFF_STATE_RESTORE(r2);;
	// branch back to SALE_CHECK
	ld8		r3=[r2];;
	mov		b0=r3;;		// SAL_CHECK return address

	// release lock
	movl		r3=ia64_mca_serialize;;
	DATA_VA_TO_PA(r3);;
	st8.rel		[r3]=r0

	br		b0
	;;
ia64_os_mca_dispatch_end:
//EndMain//////////////////////////////////////////////////////////////////////


//++
// Name:
//      ia64_os_mca_proc_state_dump()
//
// Stub Description:
//
//       This stub dumps the processor state during MCHK to a data area
//
//--

ia64_os_mca_proc_state_dump:
// Save bank 1 GRs 16-31 which will be used by c-language code when we switch
//  to virtual addressing mode.
	GET_IA64_MCA_DATA(r2)
	;;
	add r2 = IA64_MCA_CPU_PROC_STATE_DUMP_OFFSET, r2
	;;
// save ar.NaT
	mov		r5=ar.unat                  // ar.unat

// save banked GRs 16-31 along with NaT bits
	bsw.1;;
	st8.spill	[r2]=r16,8;;
	st8.spill	[r2]=r17,8;;
	st8.spill	[r2]=r18,8;;
	st8.spill	[r2]=r19,8;;
	st8.spill	[r2]=r20,8;;
	st8.spill	[r2]=r21,8;;
	st8.spill	[r2]=r22,8;;
	st8.spill	[r2]=r23,8;;
	st8.spill	[r2]=r24,8;;
	st8.spill	[r2]=r25,8;;
	st8.spill	[r2]=r26,8;;
	st8.spill	[r2]=r27,8;;
	st8.spill	[r2]=r28,8;;
	st8.spill	[r2]=r29,8;;
	st8.spill	[r2]=r30,8;;
	st8.spill	[r2]=r31,8;;

	mov		r4=ar.unat;;
	st8		[r2]=r4,8                // save User NaT bits for r16-r31
	mov		ar.unat=r5                  // restore original unat
	bsw.0;;

//save BRs
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2                // duplicate r2 in r4

	mov		r3=b0
	mov		r5=b1
	mov		r7=b2;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=b3
	mov		r5=b4
	mov		r7=b5;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=b6
	mov		r5=b7;;
	st8		[r2]=r3,2*8
	st8		[r4]=r5,2*8;;

cSaveCRs:
// save CRs
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2                // duplicate r2 in r4

	mov		r3=cr.dcr
	mov		r5=cr.itm
	mov		r7=cr.iva;;

	st8		[r2]=r3,8*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;            // 48 byte rements

	mov		r3=cr.pta;;
	st8		[r2]=r3,8*8;;            // 64 byte rements

// if PSR.ic=0, reading interruption registers causes an illegal operation fault
	mov		r3=psr;;
	tbit.nz.unc	p6,p0=r3,PSR_IC;;           // PSI Valid Log bit pos. test
(p6)    st8     [r2]=r0,9*8+160             // increment by 232 byte inc.
begin_skip_intr_regs:
(p6)	br		SkipIntrRegs;;

	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2                // duplicate r2 in r6

	mov		r3=cr.ipsr
	mov		r5=cr.isr
	mov		r7=r0;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=cr.iip
	mov		r5=cr.ifa
	mov		r7=cr.itir;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=cr.iipa
	mov		r5=cr.ifs
	mov		r7=cr.iim;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=cr25;;                   // cr.iha
	st8		[r2]=r3,160;;               // 160 byte rement

SkipIntrRegs:
	st8		[r2]=r0,152;;               // another 152 byte .

	add		r4=8,r2                     // duplicate r2 in r4
	add		r6=2*8,r2                   // duplicate r2 in r6

	mov		r3=cr.lid
//	mov		r5=cr.ivr                     // cr.ivr, don't read it
	mov		r7=cr.tpr;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=r0                       // cr.eoi => cr67
	mov		r5=r0                       // cr.irr0 => cr68
	mov		r7=r0;;                     // cr.irr1 => cr69
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=r0                       // cr.irr2 => cr70
	mov		r5=r0                       // cr.irr3 => cr71
	mov		r7=cr.itv;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=cr.pmv
	mov		r5=cr.cmcv;;
	st8		[r2]=r3,7*8
	st8		[r4]=r5,7*8;;

	mov		r3=r0                       // cr.lrr0 => cr80
	mov		r5=r0;;                     // cr.lrr1 => cr81
	st8		[r2]=r3,23*8
	st8		[r4]=r5,23*8;;

	adds		r2=25*8,r2;;

cSaveARs:
// save ARs
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2                // duplicate r2 in r6

	mov		r3=ar.k0
	mov		r5=ar.k1
	mov		r7=ar.k2;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=ar.k3
	mov		r5=ar.k4
	mov		r7=ar.k5;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=ar.k6
	mov		r5=ar.k7
	mov		r7=r0;;                     // ar.kr8
	st8		[r2]=r3,10*8
	st8		[r4]=r5,10*8
	st8		[r6]=r7,10*8;;           // rement by 72 bytes

	mov		r3=ar.rsc
	mov		ar.rsc=r0			    // put RSE in enforced lazy mode
	mov		r5=ar.bsp
	;;
	mov		r7=ar.bspstore;;
	st8		[r2]=r3,3*8
	st8		[r4]=r5,3*8
	st8		[r6]=r7,3*8;;

	mov		r3=ar.rnat;;
	st8		[r2]=r3,8*13             // increment by 13x8 bytes

	mov		r3=ar.ccv;;
	st8		[r2]=r3,8*4

	mov		r3=ar.unat;;
	st8		[r2]=r3,8*4

	mov		r3=ar.fpsr;;
	st8		[r2]=r3,8*4

	mov		r3=ar.itc;;
	st8		[r2]=r3,160                 // 160

	mov		r3=ar.pfs;;
	st8		[r2]=r3,8

	mov		r3=ar.lc;;
	st8		[r2]=r3,8

	mov		r3=ar.ec;;
	st8		[r2]=r3
	add		r2=8*62,r2               //padding

// save RRs
	mov		ar.lc=0x08-1
	movl		r4=0x00;;

cStRR:
	dep.z		r5=r4,61,3;;
	mov		r3=rr[r5];;
	st8		[r2]=r3,8
	add		r4=1,r4
	br.cloop.sptk.few	cStRR
	;;
end_os_mca_dump:
	br	ia64_os_mca_done_dump;;

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//       ia64_os_mca_proc_state_restore()
//
// Stub Description:
//
//       This is a stub to restore the saved processor state during MCHK
//
//--

ia64_os_mca_proc_state_restore:

// Restore bank1 GR16-31
	GET_IA64_MCA_DATA(r2)
	;;
	add r2 = IA64_MCA_CPU_PROC_STATE_DUMP_OFFSET, r2

restore_GRs:                                    // restore bank-1 GRs 16-31
	bsw.1;;
	add		r3=16*8,r2;;                // to get to NaT of GR 16-31
	ld8		r3=[r3];;
	mov		ar.unat=r3;;                // first restore NaT

	ld8.fill	r16=[r2],8;;
	ld8.fill	r17=[r2],8;;
	ld8.fill	r18=[r2],8;;
	ld8.fill	r19=[r2],8;;
	ld8.fill	r20=[r2],8;;
	ld8.fill	r21=[r2],8;;
	ld8.fill	r22=[r2],8;;
	ld8.fill	r23=[r2],8;;
	ld8.fill	r24=[r2],8;;
	ld8.fill	r25=[r2],8;;
	ld8.fill	r26=[r2],8;;
	ld8.fill	r27=[r2],8;;
	ld8.fill	r28=[r2],8;;
	ld8.fill	r29=[r2],8;;
	ld8.fill	r30=[r2],8;;
	ld8.fill	r31=[r2],8;;

	ld8		r3=[r2],8;;              // increment to skip NaT
	bsw.0;;

restore_BRs:
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2;;              // duplicate r2 in r4

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		b0=r3
	mov		b1=r5
	mov		b2=r7;;

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		b3=r3
	mov		b4=r5
	mov		b5=r7;;

	ld8		r3=[r2],2*8
	ld8		r5=[r4],2*8;;
	mov		b6=r3
	mov		b7=r5;;

restore_CRs:
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2;;              // duplicate r2 in r4

	ld8		r3=[r2],8*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;            // 48 byte increments
	mov		cr.dcr=r3
	mov		cr.itm=r5
	mov		cr.iva=r7;;

	ld8		r3=[r2],8*8;;            // 64 byte increments
//      mov		cr.pta=r3


// if PSR.ic=1, reading interruption registers causes an illegal operation fault
	mov		r3=psr;;
	tbit.nz.unc	p6,p0=r3,PSR_IC;;           // PSI Valid Log bit pos. test
(p6)    st8     [r2]=r0,9*8+160             // increment by 232 byte inc.

begin_rskip_intr_regs:
(p6)	br		rSkipIntrRegs;;

	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2;;              // duplicate r2 in r4

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		cr.ipsr=r3
//	mov		cr.isr=r5                   // cr.isr is read only

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		cr.iip=r3
	mov		cr.ifa=r5
	mov		cr.itir=r7;;

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		cr.iipa=r3
	mov		cr.ifs=r5
	mov		cr.iim=r7

	ld8		r3=[r2],160;;               // 160 byte increment
	mov		cr.iha=r3

rSkipIntrRegs:
	ld8		r3=[r2],152;;               // another 152 byte inc.

	add		r4=8,r2                     // duplicate r2 in r4
	add		r6=2*8,r2;;                 // duplicate r2 in r6

	ld8		r3=[r2],8*3
	ld8		r5=[r4],8*3
	ld8		r7=[r6],8*3;;
	mov		cr.lid=r3
//	mov		cr.ivr=r5                   // cr.ivr is read only
	mov		cr.tpr=r7;;

	ld8		r3=[r2],8*3
	ld8		r5=[r4],8*3
	ld8		r7=[r6],8*3;;
//	mov		cr.eoi=r3
//	mov		cr.irr0=r5                  // cr.irr0 is read only
//	mov		cr.irr1=r7;;                // cr.irr1 is read only

	ld8		r3=[r2],8*3
	ld8		r5=[r4],8*3
	ld8		r7=[r6],8*3;;
//	mov		cr.irr2=r3                  // cr.irr2 is read only
//	mov		cr.irr3=r5                  // cr.irr3 is read only
	mov		cr.itv=r7;;

	ld8		r3=[r2],8*7
	ld8		r5=[r4],8*7;;
	mov		cr.pmv=r3
	mov		cr.cmcv=r5;;

	ld8		r3=[r2],8*23
	ld8		r5=[r4],8*23;;
	adds		r2=8*23,r2
	adds		r4=8*23,r4;;
//	mov		cr.lrr0=r3
//	mov		cr.lrr1=r5

	adds		r2=8*2,r2;;

restore_ARs:
	add		r4=8,r2                  // duplicate r2 in r4
	add		r6=2*8,r2;;              // duplicate r2 in r4

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		ar.k0=r3
	mov		ar.k1=r5
	mov		ar.k2=r7;;

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
	mov		ar.k3=r3
	mov		ar.k4=r5
	mov		ar.k5=r7;;

	ld8		r3=[r2],10*8
	ld8		r5=[r4],10*8
	ld8		r7=[r6],10*8;;
	mov		ar.k6=r3
	mov		ar.k7=r5
	;;

	ld8		r3=[r2],3*8
	ld8		r5=[r4],3*8
	ld8		r7=[r6],3*8;;
//	mov		ar.rsc=r3
//	mov		ar.bsp=r5                   // ar.bsp is read only
	mov		ar.rsc=r0			    // make sure that RSE is in enforced lazy mode
	;;
	mov		ar.bspstore=r7;;

	ld8		r9=[r2],8*13;;
	mov		ar.rnat=r9

	mov		ar.rsc=r3
	ld8		r3=[r2],8*4;;
	mov		ar.ccv=r3

	ld8		r3=[r2],8*4;;
	mov		ar.unat=r3

	ld8		r3=[r2],8*4;;
	mov		ar.fpsr=r3

	ld8		r3=[r2],160;;               // 160
//      mov		ar.itc=r3

	ld8		r3=[r2],8;;
	mov		ar.pfs=r3

	ld8		r3=[r2],8;;
	mov		ar.lc=r3

	ld8		r3=[r2];;
	mov		ar.ec=r3
	add		r2=8*62,r2;;             // padding

restore_RRs:
	mov		r5=ar.lc
	mov		ar.lc=0x08-1
	movl		r4=0x00;;
cStRRr:
	dep.z		r7=r4,61,3
	ld8		r3=[r2],8;;
	mov		rr[r7]=r3                   // what are its access previledges?
	add		r4=1,r4
	br.cloop.sptk.few	cStRRr
	;;
	mov		ar.lc=r5
	;;
end_os_mca_restore:
	br	ia64_os_mca_done_restore;;

//EndStub//////////////////////////////////////////////////////////////////////


// ok, the issue here is that we need to save state information so
// it can be useable by the kernel debugger and show regs routines.
// In order to do this, our best bet is save the current state (plus
// the state information obtain from the MIN_STATE_AREA) into a pt_regs
// format.  This way we can pass it on in a useable format.
//

//
// SAL to OS entry point for INIT on the monarch processor
// This has been defined for registration purposes with SAL
// as a part of ia64_mca_init.
//
// When we get here, the following registers have been
// set by the SAL for our use
//
//		1. GR1 = OS INIT GP
//		2. GR8 = PAL_PROC physical address
//		3. GR9 = SAL_PROC physical address
//		4. GR10 = SAL GP (physical)
//		5. GR11 = Init Reason
//			0 = Received INIT for event other than crash dump switch
//			1 = Received wakeup at the end of an OS_MCA corrected machine check
//			2 = Received INIT dude to CrashDump switch assertion
//
//		6. GR12 = Return address to location within SAL_INIT procedure


GLOBAL_ENTRY(ia64_monarch_init_handler)
	.prologue
	// stash the information the SAL passed to os
	SAL_TO_OS_MCA_HANDOFF_STATE_SAVE(r2)
	;;
	SAVE_MIN_WITH_COVER
	;;
	mov r8=cr.ifa
	mov r9=cr.isr
	adds r3=8,r2				// set up second base pointer
	;;
	SAVE_REST

// ok, enough should be saved at this point to be dangerous, and supply
// information for a dump
// We need to switch to Virtual mode before hitting the C functions.

	movl	r2=IA64_PSR_IT|IA64_PSR_IC|IA64_PSR_DT|IA64_PSR_RT|IA64_PSR_DFH|IA64_PSR_BN
	mov	r3=psr	// get the current psr, minimum enabled at this point
	;;
	or	r2=r2,r3
	;;
	movl	r3=IVirtual_Switch
	;;
	mov	cr.iip=r3	// short return to set the appropriate bits
	mov	cr.ipsr=r2	// need to do an rfi to set appropriate bits
	;;
	rfi
	;;
IVirtual_Switch:
	//
	// We should now be running virtual
	//
	// Let's call the C handler to get the rest of the state info
	//
	alloc r14=ar.pfs,0,0,2,0		// now it's safe (must be first in insn group!)
	;;
	adds out0=16,sp				// out0 = pointer to pt_regs
	;;
	DO_SAVE_SWITCH_STACK
	.body
	adds out1=16,sp				// out0 = pointer to switch_stack

	br.call.sptk.many rp=ia64_init_handler
.ret1:

return_from_init:
	br.sptk return_from_init
END(ia64_monarch_init_handler)

//
// SAL to OS entry point for INIT on the slave processor
// This has been defined for registration purposes with SAL
// as a part of ia64_mca_init.
//

GLOBAL_ENTRY(ia64_slave_init_handler)
1:	br.sptk 1b
END(ia64_slave_init_handler)
