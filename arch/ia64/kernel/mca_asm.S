/*
 * File:	mca_asm.S
 * Purpose:	assembly portion of the IA64 MCA handling
 *
 * Mods by cfleck to integrate into kernel build
 *
 * 2000-03-15 David Mosberger-Tang <davidm@hpl.hp.com>
 *		Added various stop bits to get a clean compile
 *
 * 2000-03-29 Chuck Fleckenstein <cfleck@co.intel.com>
 *		Added code to save INIT handoff state in pt_regs format,
 *		switch to temp kstack, switch modes, jump to C INIT handler
 *
 * 2002-01-04 J.Hall <jenna.s.hall@intel.com>
 *		Before entering virtual mode code:
 *		 1. Check for TLB CPU error
 *		 2. Restore current thread pointer to kr6
 *		 3. Move stack ptr 16 bytes to conform to C calling convention
 *
 * 2004-11-12 Russ Anderson <rja@sgi.com>
 *		Added per cpu MCA/INIT stack save areas.
 *
 * 2005-12-08 Keith Owens <kaos@sgi.com>
 *		Use per cpu MCA/INIT stacks for all data.
 */
#include <linux/threads.h>

#include <asm/asmmacro.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mca_asm.h>
#include <asm/mca.h>

#include "entry.h"

#define GET_IA64_MCA_DATA(reg)						\
	GET_THIS_PADDR(reg, ia64_mca_data)				\
	;;								\
	ld8 reg=[reg]

	.global ia64_do_tlb_purge
	.global ia64_os_mca_dispatch
	.global ia64_os_init_dispatch_monarch
	.global ia64_os_init_dispatch_slave

	.text
	.align 16

//StartMain////////////////////////////////////////////////////////////////////

/*
 * Just the TLB purge part is moved to a separate function
 * so we can re-use the code for cpu hotplug code as well
 * Caller should now setup b1, so we can branch once the
 * tlb flush is complete.
 */

ia64_do_tlb_purge:
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
	// Now branch away to caller.
	br.sptk.many b1
	;;

//EndMain//////////////////////////////////////////////////////////////////////

//StartMain////////////////////////////////////////////////////////////////////

ia64_os_mca_dispatch:
	mov r3=IA64_MCA_CPU_MCA_STACK_OFFSET	// use the MCA stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	mov r19=1				// All MCA events are treated as monarch (for now)
	br.sptk ia64_state_save			// save the state that is not in minstate
1:

	GET_IA64_MCA_DATA(r2)
	// Using MCA stack, struct ia64_sal_os_state, variable proc_state_param
	;;
	add r3=IA64_MCA_CPU_MCA_STACK_OFFSET+MCA_SOS_OFFSET+SOS(PROC_STATE_PARAM), r2
	;;
	ld8 r18=[r3]				// Get processor state parameter on existing PALE_CHECK.
	;;
	tbit.nz p6,p7=r18,60
(p7)	br.spnt done_tlb_purge_and_reload

	// The following code purges TC and TR entries. Then reload all TC entries.
	// Purge percpu data TC entries.
begin_tlb_purge_and_reload:
	movl r18=ia64_reload_tr;;
	LOAD_PHYSICAL(p0,r18,ia64_reload_tr);;
	mov b1=r18;;
	br.sptk.many ia64_do_tlb_purge;;

ia64_reload_tr:
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
	GET_THIS_PADDR(r2, ia64_mca_tr_reload)
	mov r18 = 1
	;;
	srlz.d
	;;
	st8 [r2] =r18
	;;

done_tlb_purge_and_reload:

	// switch to per cpu MCA stack
	mov r3=IA64_MCA_CPU_MCA_STACK_OFFSET	// use the MCA stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_new_stack
1:

	// everything saved, now we can set the kernel registers
	mov r3=IA64_MCA_CPU_MCA_STACK_OFFSET	// use the MCA stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_set_kernel_registers
1:

	// This must be done in physical mode
	GET_IA64_MCA_DATA(r2)
	;;
	mov r7=r2

        // Enter virtual mode from physical mode
	VIRTUAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_begin, r4)

	// This code returns to SAL via SOS r2, in general SAL has no unwind
	// data.  To get a clean termination when backtracing the C MCA/INIT
	// handler, set a dummy return address of 0 in this routine.  That
	// requires that ia64_os_mca_virtual_begin be a global function.
ENTRY(ia64_os_mca_virtual_begin)
	.prologue
	.save rp,r0
	.body

	mov ar.rsc=3				// set eager mode for C handler
	mov r2=r7				// see GET_IA64_MCA_DATA above
	;;

	// Call virtual mode handler
	alloc r14=ar.pfs,0,0,3,0
	;;
	DATA_PA_TO_VA(r2,r7)
	;;
	add out0=IA64_MCA_CPU_MCA_STACK_OFFSET+MCA_PT_REGS_OFFSET, r2
	add out1=IA64_MCA_CPU_MCA_STACK_OFFSET+MCA_SWITCH_STACK_OFFSET, r2
	add out2=IA64_MCA_CPU_MCA_STACK_OFFSET+MCA_SOS_OFFSET, r2
	br.call.sptk.many    b0=ia64_mca_handler

	// Revert back to physical mode before going back to SAL
	PHYSICAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_end, r4)
ia64_os_mca_virtual_end:

END(ia64_os_mca_virtual_begin)

	// switch back to previous stack
	alloc r14=ar.pfs,0,0,0,0		// remove the MCA handler frame
	mov r3=IA64_MCA_CPU_MCA_STACK_OFFSET	// use the MCA stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_old_stack
1:

	mov r3=IA64_MCA_CPU_MCA_STACK_OFFSET	// use the MCA stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_state_restore		// restore the SAL state
1:

	mov		b0=r12			// SAL_CHECK return address

	br		b0

//EndMain//////////////////////////////////////////////////////////////////////

//StartMain////////////////////////////////////////////////////////////////////

//
// SAL to OS entry point for INIT on all processors.  This has been defined for
// registration purposes with SAL as a part of ia64_mca_init.  Monarch and
// slave INIT have identical processing, except for the value of the
// sos->monarch flag in r19.
//

ia64_os_init_dispatch_monarch:
	mov r19=1				// Bow, bow, ye lower middle classes!
	br.sptk ia64_os_init_dispatch

ia64_os_init_dispatch_slave:
	mov r19=0				// <igor>yeth, mathter</igor>

ia64_os_init_dispatch:

	mov r3=IA64_MCA_CPU_INIT_STACK_OFFSET	// use the INIT stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_state_save			// save the state that is not in minstate
1:

	// switch to per cpu INIT stack
	mov r3=IA64_MCA_CPU_INIT_STACK_OFFSET	// use the INIT stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_new_stack
1:

	// everything saved, now we can set the kernel registers
	mov r3=IA64_MCA_CPU_INIT_STACK_OFFSET	// use the INIT stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_set_kernel_registers
1:

	// This must be done in physical mode
	GET_IA64_MCA_DATA(r2)
	;;
	mov r7=r2

        // Enter virtual mode from physical mode
	VIRTUAL_MODE_ENTER(r2, r3, ia64_os_init_virtual_begin, r4)

	// This code returns to SAL via SOS r2, in general SAL has no unwind
	// data.  To get a clean termination when backtracing the C MCA/INIT
	// handler, set a dummy return address of 0 in this routine.  That
	// requires that ia64_os_init_virtual_begin be a global function.
ENTRY(ia64_os_init_virtual_begin)
	.prologue
	.save rp,r0
	.body

	mov ar.rsc=3				// set eager mode for C handler
	mov r2=r7				// see GET_IA64_MCA_DATA above
	;;

	// Call virtual mode handler
	alloc r14=ar.pfs,0,0,3,0
	;;
	DATA_PA_TO_VA(r2,r7)
	;;
	add out0=IA64_MCA_CPU_INIT_STACK_OFFSET+MCA_PT_REGS_OFFSET, r2
	add out1=IA64_MCA_CPU_INIT_STACK_OFFSET+MCA_SWITCH_STACK_OFFSET, r2
	add out2=IA64_MCA_CPU_INIT_STACK_OFFSET+MCA_SOS_OFFSET, r2
	br.call.sptk.many    b0=ia64_init_handler

	// Revert back to physical mode before going back to SAL
	PHYSICAL_MODE_ENTER(r2, r3, ia64_os_init_virtual_end, r4)
ia64_os_init_virtual_end:

END(ia64_os_init_virtual_begin)

	mov r3=IA64_MCA_CPU_INIT_STACK_OFFSET	// use the INIT stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_state_restore		// restore the SAL state
1:

	// switch back to previous stack
	alloc r14=ar.pfs,0,0,0,0		// remove the INIT handler frame
	mov r3=IA64_MCA_CPU_INIT_STACK_OFFSET	// use the INIT stack
	LOAD_PHYSICAL(p0,r2,1f)			// return address
	br.sptk ia64_old_stack
1:

	mov		b0=r12			// SAL_CHECK return address
	br		b0

//EndMain//////////////////////////////////////////////////////////////////////

// common defines for the stubs
#define	ms		r4
#define	regs		r5
#define	temp1		r2	/* careful, it overlaps with input registers */
#define	temp2		r3	/* careful, it overlaps with input registers */
#define	temp3		r7
#define	temp4		r14


//++
// Name:
//	ia64_state_save()
//
// Stub Description:
//
//	Save the state that is not in minstate.  This is sensitive to the layout of
//	struct ia64_sal_os_state in mca.h.
//
//	r2 contains the return address, r3 contains either
//	IA64_MCA_CPU_MCA_STACK_OFFSET or IA64_MCA_CPU_INIT_STACK_OFFSET.
//
//	The OS to SAL section of struct ia64_sal_os_state is set to a default
//	value of cold boot (MCA) or warm boot (INIT) and return to the same
//	context.  ia64_sal_os_state is also used to hold some registers that
//	need to be saved and restored across the stack switches.
//
//	Most input registers to this stub come from PAL/SAL
//	r1  os gp, physical
//	r8  pal_proc entry point
//	r9  sal_proc entry point
//	r10 sal gp
//	r11 MCA - rendevzous state, INIT - reason code
//	r12 sal return address
//	r17 pal min_state
//	r18 processor state parameter
//	r19 monarch flag, set by the caller of this routine
//
//	In addition to the SAL to OS state, this routine saves all the
//	registers that appear in struct pt_regs and struct switch_stack,
//	excluding those that are already in the PAL minstate area.  This
//	results in a partial pt_regs and switch_stack, the C code copies the
//	remaining registers from PAL minstate to pt_regs and switch_stack.  The
//	resulting structures contain all the state of the original process when
//	MCA/INIT occurred.
//
//--

ia64_state_save:
	add regs=MCA_SOS_OFFSET, r3
	add ms=MCA_SOS_OFFSET+8, r3
	mov b0=r2		// save return address
	cmp.eq p1,p2=IA64_MCA_CPU_MCA_STACK_OFFSET, r3
	;;
	GET_IA64_MCA_DATA(temp2)
	;;
	add temp1=temp2, regs	// struct ia64_sal_os_state on MCA or INIT stack
	add temp2=temp2, ms	// struct ia64_sal_os_state+8 on MCA or INIT stack
	;;
	mov regs=temp1		// save the start of sos
	st8 [temp1]=r1,16	// os_gp
	st8 [temp2]=r8,16	// pal_proc
	;;
	st8 [temp1]=r9,16	// sal_proc
	st8 [temp2]=r11,16	// rv_rc
	mov r11=cr.iipa
	;;
	st8 [temp1]=r18		// proc_state_param
	st8 [temp2]=r19		// monarch
	mov r6=IA64_KR(CURRENT)
	add temp1=SOS(SAL_RA), regs
	add temp2=SOS(SAL_GP), regs
	;;
	st8 [temp1]=r12,16	// sal_ra
	st8 [temp2]=r10,16	// sal_gp
	mov r12=cr.isr
	;;
	st8 [temp1]=r17,16	// pal_min_state
	st8 [temp2]=r6,16	// prev_IA64_KR_CURRENT
	mov r6=IA64_KR(CURRENT_STACK)
	;;
	st8 [temp1]=r6,16	// prev_IA64_KR_CURRENT_STACK
	st8 [temp2]=r0,16	// prev_task, starts off as NULL
	mov r6=cr.ifa
	;;
	st8 [temp1]=r12,16	// cr.isr
	st8 [temp2]=r6,16	// cr.ifa
	mov r12=cr.itir
	;;
	st8 [temp1]=r12,16	// cr.itir
	st8 [temp2]=r11,16	// cr.iipa
	mov r12=cr.iim
	;;
	st8 [temp1]=r12		// cr.iim
(p1)	mov r12=IA64_MCA_COLD_BOOT
(p2)	mov r12=IA64_INIT_WARM_BOOT
	mov r6=cr.iha
	add temp1=SOS(OS_STATUS), regs
	;;
	st8 [temp2]=r6		// cr.iha
	add temp2=SOS(CONTEXT), regs
	st8 [temp1]=r12		// os_status, default is cold boot
	mov r6=IA64_MCA_SAME_CONTEXT
	;;
	st8 [temp2]=r6		// context, default is same context

	// Save the pt_regs data that is not in minstate.  The previous code
	// left regs at sos.
	add regs=MCA_PT_REGS_OFFSET-MCA_SOS_OFFSET, regs
	;;
	add temp1=PT(B6), regs
	mov temp3=b6
	mov temp4=b7
	add temp2=PT(B7), regs
	;;
	st8 [temp1]=temp3,PT(AR_CSD)-PT(B6)		// save b6
	st8 [temp2]=temp4,PT(AR_SSD)-PT(B7)		// save b7
	mov temp3=ar.csd
	mov temp4=ar.ssd
	cover						// must be last in group
	;;
	st8 [temp1]=temp3,PT(AR_UNAT)-PT(AR_CSD)	// save ar.csd
	st8 [temp2]=temp4,PT(AR_PFS)-PT(AR_SSD)		// save ar.ssd
	mov temp3=ar.unat
	mov temp4=ar.pfs
	;;
	st8 [temp1]=temp3,PT(AR_RNAT)-PT(AR_UNAT)	// save ar.unat
	st8 [temp2]=temp4,PT(AR_BSPSTORE)-PT(AR_PFS)	// save ar.pfs
	mov temp3=ar.rnat
	mov temp4=ar.bspstore
	;;
	st8 [temp1]=temp3,PT(LOADRS)-PT(AR_RNAT)	// save ar.rnat
	st8 [temp2]=temp4,PT(AR_FPSR)-PT(AR_BSPSTORE)	// save ar.bspstore
	mov temp3=ar.bsp
	;;
	sub temp3=temp3, temp4	// ar.bsp - ar.bspstore
	mov temp4=ar.fpsr
	;;
	shl temp3=temp3,16	// compute ar.rsc to be used for "loadrs"
	;;
	st8 [temp1]=temp3,PT(AR_CCV)-PT(LOADRS)		// save loadrs
	st8 [temp2]=temp4,PT(F6)-PT(AR_FPSR)		// save ar.fpsr
	mov temp3=ar.ccv
	;;
	st8 [temp1]=temp3,PT(F7)-PT(AR_CCV)		// save ar.ccv
	stf.spill [temp2]=f6,PT(F8)-PT(F6)
	;;
	stf.spill [temp1]=f7,PT(F9)-PT(F7)
	stf.spill [temp2]=f8,PT(F10)-PT(F8)
	;;
	stf.spill [temp1]=f9,PT(F11)-PT(F9)
	stf.spill [temp2]=f10
	;;
	stf.spill [temp1]=f11

	// Save the switch_stack data that is not in minstate nor pt_regs.  The
	// previous code left regs at pt_regs.
	add regs=MCA_SWITCH_STACK_OFFSET-MCA_PT_REGS_OFFSET, regs
	;;
	add temp1=SW(F2), regs
	add temp2=SW(F3), regs
	;;
	stf.spill [temp1]=f2,32
	stf.spill [temp2]=f3,32
	;;
	stf.spill [temp1]=f4,32
	stf.spill [temp2]=f5,32
	;;
	stf.spill [temp1]=f12,32
	stf.spill [temp2]=f13,32
	;;
	stf.spill [temp1]=f14,32
	stf.spill [temp2]=f15,32
	;;
	stf.spill [temp1]=f16,32
	stf.spill [temp2]=f17,32
	;;
	stf.spill [temp1]=f18,32
	stf.spill [temp2]=f19,32
	;;
	stf.spill [temp1]=f20,32
	stf.spill [temp2]=f21,32
	;;
	stf.spill [temp1]=f22,32
	stf.spill [temp2]=f23,32
	;;
	stf.spill [temp1]=f24,32
	stf.spill [temp2]=f25,32
	;;
	stf.spill [temp1]=f26,32
	stf.spill [temp2]=f27,32
	;;
	stf.spill [temp1]=f28,32
	stf.spill [temp2]=f29,32
	;;
	stf.spill [temp1]=f30,SW(B2)-SW(F30)
	stf.spill [temp2]=f31,SW(B3)-SW(F31)
	mov temp3=b2
	mov temp4=b3
	;;
	st8 [temp1]=temp3,16	// save b2
	st8 [temp2]=temp4,16	// save b3
	mov temp3=b4
	mov temp4=b5
	;;
	st8 [temp1]=temp3,SW(AR_LC)-SW(B4)	// save b4
	st8 [temp2]=temp4	// save b5
	mov temp3=ar.lc
	;;
	st8 [temp1]=temp3	// save ar.lc

	// FIXME: Some proms are incorrectly accessing the minstate area as
	// cached data.  The C code uses region 6, uncached virtual.  Ensure
	// that there is no cache data lying around for the first 1K of the
	// minstate area.
	// Remove this code in September 2006, that gives platforms a year to
	// fix their proms and get their customers updated.

	add r1=32*1,r17
	add r2=32*2,r17
	add r3=32*3,r17
	add r4=32*4,r17
	add r5=32*5,r17
	add r6=32*6,r17
	add r7=32*7,r17
	;;
	fc r17
	fc r1
	fc r2
	fc r3
	fc r4
	fc r5
	fc r6
	fc r7
	add r17=32*8,r17
	add r1=32*8,r1
	add r2=32*8,r2
	add r3=32*8,r3
	add r4=32*8,r4
	add r5=32*8,r5
	add r6=32*8,r6
	add r7=32*8,r7
	;;
	fc r17
	fc r1
	fc r2
	fc r3
	fc r4
	fc r5
	fc r6
	fc r7
	add r17=32*8,r17
	add r1=32*8,r1
	add r2=32*8,r2
	add r3=32*8,r3
	add r4=32*8,r4
	add r5=32*8,r5
	add r6=32*8,r6
	add r7=32*8,r7
	;;
	fc r17
	fc r1
	fc r2
	fc r3
	fc r4
	fc r5
	fc r6
	fc r7
	add r17=32*8,r17
	add r1=32*8,r1
	add r2=32*8,r2
	add r3=32*8,r3
	add r4=32*8,r4
	add r5=32*8,r5
	add r6=32*8,r6
	add r7=32*8,r7
	;;
	fc r17
	fc r1
	fc r2
	fc r3
	fc r4
	fc r5
	fc r6
	fc r7

	br.sptk b0

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//	ia64_state_restore()
//
// Stub Description:
//
//	Restore the SAL/OS state.  This is sensitive to the layout of struct
//	ia64_sal_os_state in mca.h.
//
//	r2 contains the return address, r3 contains either
//	IA64_MCA_CPU_MCA_STACK_OFFSET or IA64_MCA_CPU_INIT_STACK_OFFSET.
//
//	In addition to the SAL to OS state, this routine restores all the
//	registers that appear in struct pt_regs and struct switch_stack,
//	excluding those in the PAL minstate area.
//
//--

ia64_state_restore:
	// Restore the switch_stack data that is not in minstate nor pt_regs.
	add regs=MCA_SWITCH_STACK_OFFSET, r3
	mov b0=r2		// save return address
	;;
	GET_IA64_MCA_DATA(temp2)
	;;
	add regs=temp2, regs
	;;
	add temp1=SW(F2), regs
	add temp2=SW(F3), regs
	;;
	ldf.fill f2=[temp1],32
	ldf.fill f3=[temp2],32
	;;
	ldf.fill f4=[temp1],32
	ldf.fill f5=[temp2],32
	;;
	ldf.fill f12=[temp1],32
	ldf.fill f13=[temp2],32
	;;
	ldf.fill f14=[temp1],32
	ldf.fill f15=[temp2],32
	;;
	ldf.fill f16=[temp1],32
	ldf.fill f17=[temp2],32
	;;
	ldf.fill f18=[temp1],32
	ldf.fill f19=[temp2],32
	;;
	ldf.fill f20=[temp1],32
	ldf.fill f21=[temp2],32
	;;
	ldf.fill f22=[temp1],32
	ldf.fill f23=[temp2],32
	;;
	ldf.fill f24=[temp1],32
	ldf.fill f25=[temp2],32
	;;
	ldf.fill f26=[temp1],32
	ldf.fill f27=[temp2],32
	;;
	ldf.fill f28=[temp1],32
	ldf.fill f29=[temp2],32
	;;
	ldf.fill f30=[temp1],SW(B2)-SW(F30)
	ldf.fill f31=[temp2],SW(B3)-SW(F31)
	;;
	ld8 temp3=[temp1],16	// restore b2
	ld8 temp4=[temp2],16	// restore b3
	;;
	mov b2=temp3
	mov b3=temp4
	ld8 temp3=[temp1],SW(AR_LC)-SW(B4)	// restore b4
	ld8 temp4=[temp2]	// restore b5
	;;
	mov b4=temp3
	mov b5=temp4
	ld8 temp3=[temp1]	// restore ar.lc
	;;
	mov ar.lc=temp3

	// Restore the pt_regs data that is not in minstate.  The previous code
	// left regs at switch_stack.
	add regs=MCA_PT_REGS_OFFSET-MCA_SWITCH_STACK_OFFSET, regs
	;;
	add temp1=PT(B6), regs
	add temp2=PT(B7), regs
	;;
	ld8 temp3=[temp1],PT(AR_CSD)-PT(B6)		// restore b6
	ld8 temp4=[temp2],PT(AR_SSD)-PT(B7)		// restore b7
	;;
	mov b6=temp3
	mov b7=temp4
	ld8 temp3=[temp1],PT(AR_UNAT)-PT(AR_CSD)	// restore ar.csd
	ld8 temp4=[temp2],PT(AR_PFS)-PT(AR_SSD)		// restore ar.ssd
	;;
	mov ar.csd=temp3
	mov ar.ssd=temp4
	ld8 temp3=[temp1]				// restore ar.unat
	add temp1=PT(AR_CCV)-PT(AR_UNAT), temp1
	ld8 temp4=[temp2],PT(AR_FPSR)-PT(AR_PFS)	// restore ar.pfs
	;;
	mov ar.unat=temp3
	mov ar.pfs=temp4
	// ar.rnat, ar.bspstore, loadrs are restore in ia64_old_stack.
	ld8 temp3=[temp1],PT(F6)-PT(AR_CCV)		// restore ar.ccv
	ld8 temp4=[temp2],PT(F7)-PT(AR_FPSR)		// restore ar.fpsr
	;;
	mov ar.ccv=temp3
	mov ar.fpsr=temp4
	ldf.fill f6=[temp1],PT(F8)-PT(F6)
	ldf.fill f7=[temp2],PT(F9)-PT(F7)
	;;
	ldf.fill f8=[temp1],PT(F10)-PT(F8)
	ldf.fill f9=[temp2],PT(F11)-PT(F9)
	;;
	ldf.fill f10=[temp1]
	ldf.fill f11=[temp2]

	// Restore the SAL to OS state. The previous code left regs at pt_regs.
	add regs=MCA_SOS_OFFSET-MCA_PT_REGS_OFFSET, regs
	;;
	add temp1=SOS(SAL_RA), regs
	add temp2=SOS(SAL_GP), regs
	;;
	ld8 r12=[temp1],16	// sal_ra
	ld8 r9=[temp2],16	// sal_gp
	;;
	ld8 r22=[temp1],16	// pal_min_state, virtual
	ld8 r13=[temp2],16	// prev_IA64_KR_CURRENT
	;;
	ld8 r16=[temp1],16	// prev_IA64_KR_CURRENT_STACK
	ld8 r20=[temp2],16	// prev_task
	;;
	ld8 temp3=[temp1],16	// cr.isr
	ld8 temp4=[temp2],16	// cr.ifa
	;;
	mov cr.isr=temp3
	mov cr.ifa=temp4
	ld8 temp3=[temp1],16	// cr.itir
	ld8 temp4=[temp2],16	// cr.iipa
	;;
	mov cr.itir=temp3
	mov cr.iipa=temp4
	ld8 temp3=[temp1]	// cr.iim
	ld8 temp4=[temp2]		// cr.iha
	add temp1=SOS(OS_STATUS), regs
	add temp2=SOS(CONTEXT), regs
	;;
	mov cr.iim=temp3
	mov cr.iha=temp4
	dep r22=0,r22,62,1	// pal_min_state, physical, uncached
	mov IA64_KR(CURRENT)=r13
	ld8 r8=[temp1]		// os_status
	ld8 r10=[temp2]		// context

	/* Wire IA64_TR_CURRENT_STACK to the stack that we are resuming to.  To
	 * avoid any dependencies on the algorithm in ia64_switch_to(), just
	 * purge any existing CURRENT_STACK mapping and insert the new one.
	 *
	 * r16 contains prev_IA64_KR_CURRENT_STACK, r13 contains
	 * prev_IA64_KR_CURRENT, these values may have been changed by the C
	 * code.  Do not use r8, r9, r10, r22, they contain values ready for
	 * the return to SAL.
	 */

	mov r15=IA64_KR(CURRENT_STACK)		// physical granule mapped by IA64_TR_CURRENT_STACK
	;;
	shl r15=r15,IA64_GRANULE_SHIFT
	;;
	dep r15=-1,r15,61,3			// virtual granule
	mov r18=IA64_GRANULE_SHIFT<<2		// for cr.itir.ps
	;;
	ptr.d r15,r18
	;;
	srlz.d

	extr.u r19=r13,61,3			// r13 = prev_IA64_KR_CURRENT
	shl r20=r16,IA64_GRANULE_SHIFT		// r16 = prev_IA64_KR_CURRENT_STACK
	movl r21=PAGE_KERNEL			// page properties
	;;
	mov IA64_KR(CURRENT_STACK)=r16
	cmp.ne p6,p0=RGN_KERNEL,r19		// new stack is in the kernel region?
	or r21=r20,r21				// construct PA | page properties
(p6)	br.spnt 1f				// the dreaded cpu 0 idle task in region 5:(
	;;
	mov cr.itir=r18
	mov cr.ifa=r13
	mov r20=IA64_TR_CURRENT_STACK
	;;
	itr.d dtr[r20]=r21
	;;
	srlz.d
1:

	br.sptk b0

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//	ia64_new_stack()
//
// Stub Description:
//
//	Switch to the MCA/INIT stack.
//
//	r2 contains the return address, r3 contains either
//	IA64_MCA_CPU_MCA_STACK_OFFSET or IA64_MCA_CPU_INIT_STACK_OFFSET.
//
//	On entry RBS is still on the original stack, this routine switches RBS
//	to use the MCA/INIT stack.
//
//	On entry, sos->pal_min_state is physical, on exit it is virtual.
//
//--

ia64_new_stack:
	add regs=MCA_PT_REGS_OFFSET, r3
	add temp2=MCA_SOS_OFFSET+SOS(PAL_MIN_STATE), r3
	mov b0=r2			// save return address
	GET_IA64_MCA_DATA(temp1)
	invala
	;;
	add temp2=temp2, temp1		// struct ia64_sal_os_state.pal_min_state on MCA or INIT stack
	add regs=regs, temp1		// struct pt_regs on MCA or INIT stack
	;;
	// Address of minstate area provided by PAL is physical, uncacheable.
	// Convert to Linux virtual address in region 6 for C code.
	ld8 ms=[temp2]			// pal_min_state, physical
	;;
	dep temp1=-1,ms,62,2		// set region 6
	mov temp3=IA64_RBS_OFFSET-MCA_PT_REGS_OFFSET
	;;
	st8 [temp2]=temp1		// pal_min_state, virtual

	add temp4=temp3, regs		// start of bspstore on new stack
	;;
	mov ar.bspstore=temp4		// switch RBS to MCA/INIT stack
	;;
	flushrs				// must be first in group
	br.sptk b0

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//	ia64_old_stack()
//
// Stub Description:
//
//	Switch to the old stack.
//
//	r2 contains the return address, r3 contains either
//	IA64_MCA_CPU_MCA_STACK_OFFSET or IA64_MCA_CPU_INIT_STACK_OFFSET.
//
//	On entry, pal_min_state is virtual, on exit it is physical.
//
//	On entry RBS is on the MCA/INIT stack, this routine switches RBS
//	back to the previous stack.
//
//	The psr is set to all zeroes.  SAL return requires either all zeroes or
//	just psr.mc set.  Leaving psr.mc off allows INIT to be issued if this
//	code does not perform correctly.
//
//	The dirty registers at the time of the event were flushed to the
//	MCA/INIT stack in ia64_pt_regs_save().  Restore the dirty registers
//	before reverting to the previous bspstore.
//--

ia64_old_stack:
	add regs=MCA_PT_REGS_OFFSET, r3
	mov b0=r2			// save return address
	GET_IA64_MCA_DATA(temp2)
	LOAD_PHYSICAL(p0,temp1,1f)
	;;
	mov cr.ipsr=r0
	mov cr.ifs=r0
	mov cr.iip=temp1
	;;
	invala
	rfi
1:

	add regs=regs, temp2		// struct pt_regs on MCA or INIT stack
	;;
	add temp1=PT(LOADRS), regs
	;;
	ld8 temp2=[temp1],PT(AR_BSPSTORE)-PT(LOADRS)	// restore loadrs
	;;
	ld8 temp3=[temp1],PT(AR_RNAT)-PT(AR_BSPSTORE)	// restore ar.bspstore
	mov ar.rsc=temp2
	;;
	loadrs
	ld8 temp4=[temp1]		// restore ar.rnat
	;;
	mov ar.bspstore=temp3		// back to old stack
	;;
	mov ar.rnat=temp4
	;;

	br.sptk b0

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//	ia64_set_kernel_registers()
//
// Stub Description:
//
//	Set the registers that are required by the C code in order to run on an
//	MCA/INIT stack.
//
//	r2 contains the return address, r3 contains either
//	IA64_MCA_CPU_MCA_STACK_OFFSET or IA64_MCA_CPU_INIT_STACK_OFFSET.
//
//--

ia64_set_kernel_registers:
	add temp3=MCA_SP_OFFSET, r3
	mov b0=r2		// save return address
	GET_IA64_MCA_DATA(temp1)
	;;
	add r12=temp1, temp3	// kernel stack pointer on MCA/INIT stack
	add r13=temp1, r3	// set current to start of MCA/INIT stack
	add r20=temp1, r3	// physical start of MCA/INIT stack
	;;
	DATA_PA_TO_VA(r12,temp2)
	DATA_PA_TO_VA(r13,temp3)
	;;
	mov IA64_KR(CURRENT)=r13

	/* Wire IA64_TR_CURRENT_STACK to the MCA/INIT handler stack.  To avoid
	 * any dependencies on the algorithm in ia64_switch_to(), just purge
	 * any existing CURRENT_STACK mapping and insert the new one.
	 */

	mov r16=IA64_KR(CURRENT_STACK)		// physical granule mapped by IA64_TR_CURRENT_STACK
	;;
	shl r16=r16,IA64_GRANULE_SHIFT
	;;
	dep r16=-1,r16,61,3			// virtual granule
	mov r18=IA64_GRANULE_SHIFT<<2		// for cr.itir.ps
	;;
	ptr.d r16,r18
	;;
	srlz.d

	shr.u r16=r20,IA64_GRANULE_SHIFT	// r20 = physical start of MCA/INIT stack
	movl r21=PAGE_KERNEL			// page properties
	;;
	mov IA64_KR(CURRENT_STACK)=r16
	or r21=r20,r21				// construct PA | page properties
	;;
	mov cr.itir=r18
	mov cr.ifa=r13
	mov r20=IA64_TR_CURRENT_STACK

	movl r17=FPSR_DEFAULT
	;;
	mov.m ar.fpsr=r17			// set ar.fpsr to kernel default value
	;;
	itr.d dtr[r20]=r21
	;;
	srlz.d

	br.sptk b0

//EndStub//////////////////////////////////////////////////////////////////////

#undef	ms
#undef	regs
#undef	temp1
#undef	temp2
#undef	temp3
#undef	temp4


// Support function for mca.c, it is here to avoid using inline asm.  Given the
// address of an rnat slot, if that address is below the current ar.bspstore
// then return the contents of that slot, otherwise return the contents of
// ar.rnat.
GLOBAL_ENTRY(ia64_get_rnat)
	alloc r14=ar.pfs,1,0,0,0
	mov ar.rsc=0
	;;
	mov r14=ar.bspstore
	;;
	cmp.lt p6,p7=in0,r14
	;;
(p6)	ld8 r8=[in0]
(p7)	mov r8=ar.rnat
	mov ar.rsc=3
	br.ret.sptk.many rp
END(ia64_get_rnat)
