/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/ia64/kernel/relocate_kernel.S
 *
 * Relocate kexec'able kernel and start it
 *
 * Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2005 Khalid Aziz  <khalid.aziz@hp.com>
 * Copyright (C) 2005 Intel Corp,  Zou Nan hai <nanhai.zou@intel.com>
 */
#include <asm/asmmacro.h>
#include <asm/kregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mca_asm.h>

       /* Must be relocatable PIC code callable as a C function
        */
GLOBAL_ENTRY(relocate_new_kernel)
	.prologue
	alloc r31=ar.pfs,4,0,0,0
        .body
.reloc_entry:
{
	rsm psr.i| psr.ic
	mov r2=ip
}
	;;
{
        flushrs                         // must be first insn in group
        srlz.i
}
	;;
	dep r2=0,r2,61,3		//to physical address
	;;
	//first switch to physical mode
	add r3=1f-.reloc_entry, r2
	movl r16 = IA64_PSR_AC|IA64_PSR_BN|IA64_PSR_IC
	mov ar.rsc=0	          	// put RSE in enforced lazy mode
	;;
	add sp=(memory_stack_end - 16 - .reloc_entry),r2
	add r8=(register_stack - .reloc_entry),r2
	;;
	mov r18=ar.rnat
	mov ar.bspstore=r8
	;;
        mov cr.ipsr=r16
        mov cr.iip=r3
        mov cr.ifs=r0
	srlz.i
	;;
	mov ar.rnat=r18
	rfi				// note: this unmask MCA/INIT (psr.mc)
	;;
1:
	//physical mode code begin
	mov b6=in1
	dep r28=0,in2,61,3	//to physical address

	// purge all TC entries
#define O(member)       IA64_CPUINFO_##member##_OFFSET
        GET_THIS_PADDR(r2, ia64_cpu_info) // load phys addr of cpu_info into r2
        ;;
        addl r17=O(PTCE_STRIDE),r2
        addl r2=O(PTCE_BASE),r2
        ;;
        ld8 r18=[r2],(O(PTCE_COUNT)-O(PTCE_BASE));;    	// r18=ptce_base
        ld4 r19=[r2],4                                  // r19=ptce_count[0]
        ld4 r21=[r17],4                                 // r21=ptce_stride[0]
        ;;
        ld4 r20=[r2]                                    // r20=ptce_count[1]
        ld4 r22=[r17]                                   // r22=ptce_stride[1]
        mov r24=r0
        ;;
        adds r20=-1,r20
        ;;
#undef O
2:
        cmp.ltu p6,p7=r24,r19
(p7)    br.cond.dpnt.few 4f
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
        srlz.i
        ;;
	// purge TR entry for kernel text and data
        movl r16=KERNEL_START
        mov r18=KERNEL_TR_PAGE_SHIFT<<2
        ;;
        ptr.i r16, r18
        ptr.d r16, r18
        ;;
        srlz.i
        ;;

        // purge TR entry for pal code
        mov r16=in3
        mov r18=IA64_GRANULE_SHIFT<<2
        ;;
        ptr.i r16,r18
        ;;
        srlz.i
	;;

        // purge TR entry for stack
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

	//copy segments
	movl r16=PAGE_MASK
        mov  r30=in0                    // in0 is page_list
        br.sptk.few .dest_page
	;;
.loop:
	ld8  r30=[in0], 8;;
.dest_page:
	tbit.z p0, p6=r30, 0;;    	// 0x1 dest page
(p6)	and r17=r30, r16
(p6)	br.cond.sptk.few .loop;;

	tbit.z p0, p6=r30, 1;;		// 0x2 indirect page
(p6)	and in0=r30, r16
(p6)	br.cond.sptk.few .loop;;

	tbit.z p0, p6=r30, 2;;		// 0x4 end flag
(p6)	br.cond.sptk.few .end_loop;;

	tbit.z p6, p0=r30, 3;;		// 0x8 source page
(p6)	br.cond.sptk.few .loop

	and r18=r30, r16

	// simple copy page, may optimize later
	movl r14=PAGE_SIZE/8 - 1;;
	mov ar.lc=r14;;
1:
	ld8 r14=[r18], 8;;
	st8 [r17]=r14;;
	fc.i r17
	add r17=8, r17
	br.ctop.sptk.few 1b
	br.sptk.few .loop
	;;

.end_loop:
	sync.i			// for fc.i
	;;
	srlz.i
	;;
	srlz.d
	;;
	br.call.sptk.many b0=b6;;

.align  32
memory_stack:
	.fill           8192, 1, 0
memory_stack_end:
register_stack:
	.fill           8192, 1, 0
register_stack_end:
relocate_new_kernel_end:
END(relocate_new_kernel)

.global relocate_new_kernel_size
relocate_new_kernel_size:
	data8	relocate_new_kernel_end - relocate_new_kernel

GLOBAL_ENTRY(ia64_dump_cpu_regs)
        .prologue
        alloc loc0=ar.pfs,1,2,0,0
        .body
        mov     ar.rsc=0                // put RSE in enforced lazy mode
        add     loc1=4*8, in0           // save r4 and r5 first
        ;;
{
        flushrs                         // flush dirty regs to backing store
        srlz.i
}
        st8 [loc1]=r4, 8
        ;;
        st8 [loc1]=r5, 8
        ;;
        add loc1=32*8, in0
        mov r4=ar.rnat
        ;;
        st8 [in0]=r0, 8			// r0
        st8 [loc1]=r4, 8		// rnat
        mov r5=pr
        ;;
        st8 [in0]=r1, 8			// r1
        st8 [loc1]=r5, 8		// pr
        mov r4=b0
        ;;
        st8 [in0]=r2, 8			// r2
        st8 [loc1]=r4, 8		// b0
        mov r5=b1;
        ;;
        st8 [in0]=r3, 24		// r3
        st8 [loc1]=r5, 8		// b1
        mov r4=b2
        ;;
        st8 [in0]=r6, 8			// r6
        st8 [loc1]=r4, 8		// b2
	mov r5=b3
        ;;
        st8 [in0]=r7, 8			// r7
        st8 [loc1]=r5, 8		// b3
        mov r4=b4
        ;;
        st8 [in0]=r8, 8			// r8
        st8 [loc1]=r4, 8		// b4
        mov r5=b5
        ;;
        st8 [in0]=r9, 8			// r9
        st8 [loc1]=r5, 8		// b5
        mov r4=b6
        ;;
        st8 [in0]=r10, 8		// r10
        st8 [loc1]=r5, 8		// b6
        mov r5=b7
        ;;
        st8 [in0]=r11, 8		// r11
        st8 [loc1]=r5, 8		// b7
        mov r4=b0
        ;;
        st8 [in0]=r12, 8		// r12
        st8 [loc1]=r4, 8		// ip
        mov r5=loc0
	;;
        st8 [in0]=r13, 8		// r13
        extr.u r5=r5, 0, 38		// ar.pfs.pfm
	mov r4=r0			// user mask
        ;;
        st8 [in0]=r14, 8		// r14
        st8 [loc1]=r5, 8		// cfm
        ;;
        st8 [in0]=r15, 8		// r15
        st8 [loc1]=r4, 8        	// user mask
	mov r5=ar.rsc
        ;;
        st8 [in0]=r16, 8		// r16
        st8 [loc1]=r5, 8        	// ar.rsc
        mov r4=ar.bsp
        ;;
        st8 [in0]=r17, 8		// r17
        st8 [loc1]=r4, 8        	// ar.bsp
        mov r5=ar.bspstore
        ;;
        st8 [in0]=r18, 8		// r18
        st8 [loc1]=r5, 8        	// ar.bspstore
        mov r4=ar.rnat
        ;;
        st8 [in0]=r19, 8		// r19
        st8 [loc1]=r4, 8        	// ar.rnat
        mov r5=ar.ccv
        ;;
        st8 [in0]=r20, 8		// r20
	st8 [loc1]=r5, 8        	// ar.ccv
        mov r4=ar.unat
        ;;
        st8 [in0]=r21, 8		// r21
        st8 [loc1]=r4, 8        	// ar.unat
        mov r5 = ar.fpsr
        ;;
        st8 [in0]=r22, 8		// r22
        st8 [loc1]=r5, 8        	// ar.fpsr
        mov r4 = ar.unat
        ;;
        st8 [in0]=r23, 8		// r23
        st8 [loc1]=r4, 8        	// unat
        mov r5 = ar.fpsr
        ;;
        st8 [in0]=r24, 8		// r24
        st8 [loc1]=r5, 8        	// fpsr
        mov r4 = ar.pfs
        ;;
        st8 [in0]=r25, 8		// r25
        st8 [loc1]=r4, 8        	// ar.pfs
        mov r5 = ar.lc
        ;;
        st8 [in0]=r26, 8		// r26
        st8 [loc1]=r5, 8        	// ar.lc
        mov r4 = ar.ec
        ;;
        st8 [in0]=r27, 8		// r27
        st8 [loc1]=r4, 8        	// ar.ec
        mov r5 = ar.csd
        ;;
        st8 [in0]=r28, 8		// r28
        st8 [loc1]=r5, 8        	// ar.csd
        mov r4 = ar.ssd
        ;;
        st8 [in0]=r29, 8		// r29
        st8 [loc1]=r4, 8        	// ar.ssd
        ;;
        st8 [in0]=r30, 8		// r30
        ;;
	st8 [in0]=r31, 8		// r31
        mov ar.pfs=loc0
        ;;
        br.ret.sptk.many rp
END(ia64_dump_cpu_regs)


