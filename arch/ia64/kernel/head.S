/*
 * Here is where the ball gets rolling as far as the kernel is concerned.
 * When control is transferred to _start, the bootload has already
 * loaded us to the correct address.  All that's left to do here is
 * to set up the kernel's global pointer and jump to the kernel
 * entry point.
 *
 * Copyright (C) 1998-2001, 2003, 2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <Don.Dugger@intel.com>
 * Copyright (C) 2002 Fenghua Yu <fenghua.yu@intel.com>
 *   -Optimize __ia64_save_fpu() and __ia64_load_fpu() for Itanium 2.
 * Copyright (C) 2004 Ashok Raj <ashok.raj@intel.com>
 *   Support for CPU Hotplug
 */


#include <asm/asmmacro.h>
#include <asm/fpu.h>
#include <asm/kregs.h>
#include <asm/mmu_context.h>
#include <asm/asm-offsets.h>
#include <asm/pal.h>
#include <asm/paravirt.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/mca_asm.h>
#include <linux/init.h>
#include <linux/linkage.h>

#ifdef CONFIG_HOTPLUG_CPU
#define SAL_PSR_BITS_TO_SET				\
	(IA64_PSR_AC | IA64_PSR_BN | IA64_PSR_MFH | IA64_PSR_MFL)

#define SAVE_FROM_REG(src, ptr, dest)	\
	mov dest=src;;						\
	st8 [ptr]=dest,0x08

#define RESTORE_REG(reg, ptr, _tmp)		\
	ld8 _tmp=[ptr],0x08;;				\
	mov reg=_tmp

#define SAVE_BREAK_REGS(ptr, _idx, _breg, _dest)\
	mov ar.lc=IA64_NUM_DBG_REGS-1;; 			\
	mov _idx=0;; 								\
1: 												\
	SAVE_FROM_REG(_breg[_idx], ptr, _dest);;	\
	add _idx=1,_idx;;							\
	br.cloop.sptk.many 1b

#define RESTORE_BREAK_REGS(ptr, _idx, _breg, _tmp, _lbl)\
	mov ar.lc=IA64_NUM_DBG_REGS-1;;			\
	mov _idx=0;;							\
_lbl:  RESTORE_REG(_breg[_idx], ptr, _tmp);;	\
	add _idx=1, _idx;;						\
	br.cloop.sptk.many _lbl

#define SAVE_ONE_RR(num, _reg, _tmp) \
	movl _tmp=(num<<61);;	\
	mov _reg=rr[_tmp]

#define SAVE_REGION_REGS(_tmp, _r0, _r1, _r2, _r3, _r4, _r5, _r6, _r7) \
	SAVE_ONE_RR(0,_r0, _tmp);; \
	SAVE_ONE_RR(1,_r1, _tmp);; \
	SAVE_ONE_RR(2,_r2, _tmp);; \
	SAVE_ONE_RR(3,_r3, _tmp);; \
	SAVE_ONE_RR(4,_r4, _tmp);; \
	SAVE_ONE_RR(5,_r5, _tmp);; \
	SAVE_ONE_RR(6,_r6, _tmp);; \
	SAVE_ONE_RR(7,_r7, _tmp);;

#define STORE_REGION_REGS(ptr, _r0, _r1, _r2, _r3, _r4, _r5, _r6, _r7) \
	st8 [ptr]=_r0, 8;; \
	st8 [ptr]=_r1, 8;; \
	st8 [ptr]=_r2, 8;; \
	st8 [ptr]=_r3, 8;; \
	st8 [ptr]=_r4, 8;; \
	st8 [ptr]=_r5, 8;; \
	st8 [ptr]=_r6, 8;; \
	st8 [ptr]=_r7, 8;;

#define RESTORE_REGION_REGS(ptr, _idx1, _idx2, _tmp) \
	mov		ar.lc=0x08-1;;						\
	movl	_idx1=0x00;;						\
RestRR:											\
	dep.z	_idx2=_idx1,61,3;;					\
	ld8		_tmp=[ptr],8;;						\
	mov		rr[_idx2]=_tmp;;					\
	srlz.d;;									\
	add		_idx1=1,_idx1;;						\
	br.cloop.sptk.few	RestRR

#define SET_AREA_FOR_BOOTING_CPU(reg1, reg2) \
	movl reg1=sal_state_for_booting_cpu;;	\
	ld8 reg2=[reg1];;

/*
 * Adjust region registers saved before starting to save
 * break regs and rest of the states that need to be preserved.
 */
#define SAL_TO_OS_BOOT_HANDOFF_STATE_SAVE(_reg1,_reg2,_pred)  \
	SAVE_FROM_REG(b0,_reg1,_reg2);;						\
	SAVE_FROM_REG(b1,_reg1,_reg2);;						\
	SAVE_FROM_REG(b2,_reg1,_reg2);;						\
	SAVE_FROM_REG(b3,_reg1,_reg2);;						\
	SAVE_FROM_REG(b4,_reg1,_reg2);;						\
	SAVE_FROM_REG(b5,_reg1,_reg2);;						\
	st8 [_reg1]=r1,0x08;;								\
	st8 [_reg1]=r12,0x08;;								\
	st8 [_reg1]=r13,0x08;;								\
	SAVE_FROM_REG(ar.fpsr,_reg1,_reg2);;				\
	SAVE_FROM_REG(ar.pfs,_reg1,_reg2);;					\
	SAVE_FROM_REG(ar.rnat,_reg1,_reg2);;				\
	SAVE_FROM_REG(ar.unat,_reg1,_reg2);;				\
	SAVE_FROM_REG(ar.bspstore,_reg1,_reg2);;			\
	SAVE_FROM_REG(cr.dcr,_reg1,_reg2);;					\
	SAVE_FROM_REG(cr.iva,_reg1,_reg2);;					\
	SAVE_FROM_REG(cr.pta,_reg1,_reg2);;					\
	SAVE_FROM_REG(cr.itv,_reg1,_reg2);;					\
	SAVE_FROM_REG(cr.pmv,_reg1,_reg2);;					\
	SAVE_FROM_REG(cr.cmcv,_reg1,_reg2);;				\
	SAVE_FROM_REG(cr.lrr0,_reg1,_reg2);;				\
	SAVE_FROM_REG(cr.lrr1,_reg1,_reg2);;				\
	st8 [_reg1]=r4,0x08;;								\
	st8 [_reg1]=r5,0x08;;								\
	st8 [_reg1]=r6,0x08;;								\
	st8 [_reg1]=r7,0x08;;								\
	st8 [_reg1]=_pred,0x08;;							\
	SAVE_FROM_REG(ar.lc, _reg1, _reg2);;				\
	stf.spill.nta [_reg1]=f2,16;;						\
	stf.spill.nta [_reg1]=f3,16;;						\
	stf.spill.nta [_reg1]=f4,16;;						\
	stf.spill.nta [_reg1]=f5,16;;						\
	stf.spill.nta [_reg1]=f16,16;;						\
	stf.spill.nta [_reg1]=f17,16;;						\
	stf.spill.nta [_reg1]=f18,16;;						\
	stf.spill.nta [_reg1]=f19,16;;						\
	stf.spill.nta [_reg1]=f20,16;;						\
	stf.spill.nta [_reg1]=f21,16;;						\
	stf.spill.nta [_reg1]=f22,16;;						\
	stf.spill.nta [_reg1]=f23,16;;						\
	stf.spill.nta [_reg1]=f24,16;;						\
	stf.spill.nta [_reg1]=f25,16;;						\
	stf.spill.nta [_reg1]=f26,16;;						\
	stf.spill.nta [_reg1]=f27,16;;						\
	stf.spill.nta [_reg1]=f28,16;;						\
	stf.spill.nta [_reg1]=f29,16;;						\
	stf.spill.nta [_reg1]=f30,16;;						\
	stf.spill.nta [_reg1]=f31,16;;

#else
#define SET_AREA_FOR_BOOTING_CPU(a1, a2)
#define SAL_TO_OS_BOOT_HANDOFF_STATE_SAVE(a1,a2, a3)
#define SAVE_REGION_REGS(_tmp, _r0, _r1, _r2, _r3, _r4, _r5, _r6, _r7)
#define STORE_REGION_REGS(ptr, _r0, _r1, _r2, _r3, _r4, _r5, _r6, _r7)
#endif

#define SET_ONE_RR(num, pgsize, _tmp1, _tmp2, vhpt) \
	movl _tmp1=(num << 61);;	\
	mov _tmp2=((ia64_rid(IA64_REGION_ID_KERNEL, (num<<61)) << 8) | (pgsize << 2) | vhpt);; \
	mov rr[_tmp1]=_tmp2

	.section __special_page_section,"ax"

	.global empty_zero_page
empty_zero_page:
	.skip PAGE_SIZE

	.global swapper_pg_dir
swapper_pg_dir:
	.skip PAGE_SIZE

	.rodata
halt_msg:
	stringz "Halting kernel\n"

	.section .text.head,"ax"

	.global start_ap

	/*
	 * Start the kernel.  When the bootloader passes control to _start(), r28
	 * points to the address of the boot parameter area.  Execution reaches
	 * here in physical mode.
	 */
GLOBAL_ENTRY(_start)
start_ap:
	.prologue
	.save rp, r0		// terminate unwind chain with a NULL rp
	.body

	rsm psr.i | psr.ic
	;;
	srlz.i
	;;
 {
	flushrs				// must be first insn in group
	srlz.i
 }
	;;
	/*
	 * Save the region registers, predicate before they get clobbered
	 */
	SAVE_REGION_REGS(r2, r8,r9,r10,r11,r12,r13,r14,r15);
	mov r25=pr;;

	/*
	 * Initialize kernel region registers:
	 *	rr[0]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[1]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[2]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[3]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[4]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[5]: VHPT enabled, page size = PAGE_SHIFT
	 *	rr[6]: VHPT disabled, page size = IA64_GRANULE_SHIFT
	 *	rr[7]: VHPT disabled, page size = IA64_GRANULE_SHIFT
	 * We initialize all of them to prevent inadvertently assuming
	 * something about the state of address translation early in boot.
	 */
	SET_ONE_RR(0, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(1, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(2, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(3, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(4, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(5, PAGE_SHIFT, r2, r16, 1);;
	SET_ONE_RR(6, IA64_GRANULE_SHIFT, r2, r16, 0);;
	SET_ONE_RR(7, IA64_GRANULE_SHIFT, r2, r16, 0);;
	/*
	 * Now pin mappings into the TLB for kernel text and data
	 */
	mov r18=KERNEL_TR_PAGE_SHIFT<<2
	movl r17=KERNEL_START
	;;
	mov cr.itir=r18
	mov cr.ifa=r17
	mov r16=IA64_TR_KERNEL
	mov r3=ip
	movl r18=PAGE_KERNEL
	;;
	dep r2=0,r3,0,KERNEL_TR_PAGE_SHIFT
	;;
	or r18=r2,r18
	;;
	srlz.i
	;;
	itr.i itr[r16]=r18
	;;
	itr.d dtr[r16]=r18
	;;
	srlz.i

	/*
	 * Switch into virtual mode:
	 */
	movl r16=(IA64_PSR_IT|IA64_PSR_IC|IA64_PSR_DT|IA64_PSR_RT|IA64_PSR_DFH|IA64_PSR_BN \
		  |IA64_PSR_DI|IA64_PSR_AC)
	;;
	mov cr.ipsr=r16
	movl r17=1f
	;;
	mov cr.iip=r17
	mov cr.ifs=r0
	;;
	rfi
	;;
1:	// now we are in virtual mode

	SET_AREA_FOR_BOOTING_CPU(r2, r16);

	STORE_REGION_REGS(r16, r8,r9,r10,r11,r12,r13,r14,r15);
	SAL_TO_OS_BOOT_HANDOFF_STATE_SAVE(r16,r17,r25)
	;;

	// set IVT entry point---can't access I/O ports without it
	movl r3=ia64_ivt
	;;
	mov cr.iva=r3
	movl r2=FPSR_DEFAULT
	;;
	srlz.i
	movl gp=__gp

	mov ar.fpsr=r2
	;;

#define isAP	p2	// are we an Application Processor?
#define isBP	p3	// are we the Bootstrap Processor?

#ifdef CONFIG_SMP
	/*
	 * Find the init_task for the currently booting CPU.  At poweron, and in
	 * UP mode, task_for_booting_cpu is NULL.
	 */
	movl r3=task_for_booting_cpu
 	;;
	ld8 r3=[r3]
	movl r2=init_task
	;;
	cmp.eq isBP,isAP=r3,r0
	;;
(isAP)	mov r2=r3
#else
	movl r2=init_task
	cmp.eq isBP,isAP=r0,r0
#endif
	;;
	tpa r3=r2		// r3 == phys addr of task struct
	mov r16=-1
(isBP)	br.cond.dpnt .load_current // BP stack is on region 5 --- no need to map it

	// load mapping for stack (virtaddr in r2, physaddr in r3)
	rsm psr.ic
	movl r17=PAGE_KERNEL
	;;
	srlz.d
	dep r18=0,r3,0,12
	;;
	or r18=r17,r18
	dep r2=-1,r3,61,3	// IMVA of task
	;;
	mov r17=rr[r2]
	shr.u r16=r3,IA64_GRANULE_SHIFT
	;;
	dep r17=0,r17,8,24
	;;
	mov cr.itir=r17
	mov cr.ifa=r2

	mov r19=IA64_TR_CURRENT_STACK
	;;
	itr.d dtr[r19]=r18
	;;
	ssm psr.ic
	srlz.d
  	;;

.load_current:
	// load the "current" pointer (r13) and ar.k6 with the current task
	mov IA64_KR(CURRENT)=r2		// virtual address
	mov IA64_KR(CURRENT_STACK)=r16
	mov r13=r2
	/*
	 * Reserve space at the top of the stack for "struct pt_regs".  Kernel
	 * threads don't store interesting values in that structure, but the space
	 * still needs to be there because time-critical stuff such as the context
	 * switching can be implemented more efficiently (for example, __switch_to()
	 * always sets the psr.dfh bit of the task it is switching to).
	 */

	addl r12=IA64_STK_OFFSET-IA64_PT_REGS_SIZE-16,r2
	addl r2=IA64_RBS_OFFSET,r2	// initialize the RSE
	mov ar.rsc=0		// place RSE in enforced lazy mode
	;;
	loadrs			// clear the dirty partition
	movl r19=__phys_per_cpu_start
	mov r18=PERCPU_PAGE_SIZE
	;;
#ifndef CONFIG_SMP
	add r19=r19,r18
	;;
#else
(isAP)	br.few 2f
	movl r20=__cpu0_per_cpu
	;;
	shr.u r18=r18,3
1:
	ld8 r21=[r19],8;;
	st8[r20]=r21,8
	adds r18=-1,r18;;
	cmp4.lt p7,p6=0,r18
(p7)	br.cond.dptk.few 1b
	mov r19=r20
	;;
2:
#endif
	tpa r19=r19
	;;
	.pred.rel.mutex isBP,isAP
(isBP)	mov IA64_KR(PER_CPU_DATA)=r19	// per-CPU base for cpu0
(isAP)	mov IA64_KR(PER_CPU_DATA)=r0	// clear physical per-CPU base
	;;
	mov ar.bspstore=r2	// establish the new RSE stack
	;;
	mov ar.rsc=0x3		// place RSE in eager mode

(isBP)	dep r28=-1,r28,61,3	// make address virtual
(isBP)	movl r2=ia64_boot_param
	;;
(isBP)	st8 [r2]=r28		// save the address of the boot param area passed by the bootloader

#ifdef CONFIG_PARAVIRT

	movl r14=hypervisor_setup_hooks
	movl r15=hypervisor_type
	mov r16=num_hypervisor_hooks
	;;
	ld8 r2=[r15]
	;;
	cmp.ltu p7,p0=r2,r16	// array size check
	shladd r8=r2,3,r14
	;;
(p7)	ld8 r9=[r8]
	;;
(p7)	mov b1=r9
(p7)	cmp.ne.unc p7,p0=r9,r0	// no actual branch to NULL
	;;
(p7)	br.call.sptk.many rp=b1

	__INITDATA

default_setup_hook = 0		// Currently nothing needs to be done.

	.weak xen_setup_hook

	.global hypervisor_type
hypervisor_type:
	data8		PARAVIRT_HYPERVISOR_TYPE_DEFAULT

	// must have the same order with PARAVIRT_HYPERVISOR_TYPE_xxx

hypervisor_setup_hooks:
	data8		default_setup_hook
	data8		xen_setup_hook
num_hypervisor_hooks = (. - hypervisor_setup_hooks) / 8
	.previous

#endif

#ifdef CONFIG_SMP
(isAP)	br.call.sptk.many rp=start_secondary
.ret0:
(isAP)	br.cond.sptk self
#endif

	// This is executed by the bootstrap processor (bsp) only:

#ifdef CONFIG_IA64_FW_EMU
	// initialize PAL & SAL emulator:
	br.call.sptk.many rp=sys_fw_init
.ret1:
#endif
	br.call.sptk.many rp=start_kernel
.ret2:	addl r3=@ltoff(halt_msg),gp
	;;
	alloc r2=ar.pfs,8,0,2,0
	;;
	ld8 out0=[r3]
	br.call.sptk.many b0=console_print

self:	hint @pause
	br.sptk.many self		// endless loop
END(_start)

	.text

GLOBAL_ENTRY(ia64_save_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
	mov r20=ar.lc			// preserve ar.lc
	mov ar.lc=IA64_NUM_DBG_REGS-1
	mov r18=0
	add r19=IA64_NUM_DBG_REGS*8,in0
	;;
1:	mov r16=dbr[r18]
#ifdef CONFIG_ITANIUM
	;;
	srlz.d
#endif
	mov r17=ibr[r18]
	add r18=1,r18
	;;
	st8.nta [in0]=r16,8
	st8.nta [r19]=r17,8
	br.cloop.sptk.many 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.many rp
END(ia64_save_debug_regs)

GLOBAL_ENTRY(ia64_load_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
	lfetch.nta [in0]
	mov r20=ar.lc			// preserve ar.lc
	add r19=IA64_NUM_DBG_REGS*8,in0
	mov ar.lc=IA64_NUM_DBG_REGS-1
	mov r18=-1
	;;
1:	ld8.nta r16=[in0],8
	ld8.nta r17=[r19],8
	add r18=1,r18
	;;
	mov dbr[r18]=r16
#ifdef CONFIG_ITANIUM
	;;
	srlz.d				// Errata 132 (NoFix status)
#endif
	mov ibr[r18]=r17
	br.cloop.sptk.many 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.many rp
END(ia64_load_debug_regs)

GLOBAL_ENTRY(__ia64_save_fpu)
	alloc r2=ar.pfs,1,4,0,0
	adds loc0=96*16-16,in0
	adds loc1=96*16-16-128,in0
	;;
	stf.spill.nta [loc0]=f127,-256
	stf.spill.nta [loc1]=f119,-256
	;;
	stf.spill.nta [loc0]=f111,-256
	stf.spill.nta [loc1]=f103,-256
	;;
	stf.spill.nta [loc0]=f95,-256
	stf.spill.nta [loc1]=f87,-256
	;;
	stf.spill.nta [loc0]=f79,-256
	stf.spill.nta [loc1]=f71,-256
	;;
	stf.spill.nta [loc0]=f63,-256
	stf.spill.nta [loc1]=f55,-256
	adds loc2=96*16-32,in0
	;;
	stf.spill.nta [loc0]=f47,-256
	stf.spill.nta [loc1]=f39,-256
	adds loc3=96*16-32-128,in0
	;;
	stf.spill.nta [loc2]=f126,-256
	stf.spill.nta [loc3]=f118,-256
	;;
	stf.spill.nta [loc2]=f110,-256
	stf.spill.nta [loc3]=f102,-256
	;;
	stf.spill.nta [loc2]=f94,-256
	stf.spill.nta [loc3]=f86,-256
	;;
	stf.spill.nta [loc2]=f78,-256
	stf.spill.nta [loc3]=f70,-256
	;;
	stf.spill.nta [loc2]=f62,-256
	stf.spill.nta [loc3]=f54,-256
	adds loc0=96*16-48,in0
	;;
	stf.spill.nta [loc2]=f46,-256
	stf.spill.nta [loc3]=f38,-256
	adds loc1=96*16-48-128,in0
	;;
	stf.spill.nta [loc0]=f125,-256
	stf.spill.nta [loc1]=f117,-256
	;;
	stf.spill.nta [loc0]=f109,-256
	stf.spill.nta [loc1]=f101,-256
	;;
	stf.spill.nta [loc0]=f93,-256
	stf.spill.nta [loc1]=f85,-256
	;;
	stf.spill.nta [loc0]=f77,-256
	stf.spill.nta [loc1]=f69,-256
	;;
	stf.spill.nta [loc0]=f61,-256
	stf.spill.nta [loc1]=f53,-256
	adds loc2=96*16-64,in0
	;;
	stf.spill.nta [loc0]=f45,-256
	stf.spill.nta [loc1]=f37,-256
	adds loc3=96*16-64-128,in0
	;;
	stf.spill.nta [loc2]=f124,-256
	stf.spill.nta [loc3]=f116,-256
	;;
	stf.spill.nta [loc2]=f108,-256
	stf.spill.nta [loc3]=f100,-256
	;;
	stf.spill.nta [loc2]=f92,-256
	stf.spill.nta [loc3]=f84,-256
	;;
	stf.spill.nta [loc2]=f76,-256
	stf.spill.nta [loc3]=f68,-256
	;;
	stf.spill.nta [loc2]=f60,-256
	stf.spill.nta [loc3]=f52,-256
	adds loc0=96*16-80,in0
	;;
	stf.spill.nta [loc2]=f44,-256
	stf.spill.nta [loc3]=f36,-256
	adds loc1=96*16-80-128,in0
	;;
	stf.spill.nta [loc0]=f123,-256
	stf.spill.nta [loc1]=f115,-256
	;;
	stf.spill.nta [loc0]=f107,-256
	stf.spill.nta [loc1]=f99,-256
	;;
	stf.spill.nta [loc0]=f91,-256
	stf.spill.nta [loc1]=f83,-256
	;;
	stf.spill.nta [loc0]=f75,-256
	stf.spill.nta [loc1]=f67,-256
	;;
	stf.spill.nta [loc0]=f59,-256
	stf.spill.nta [loc1]=f51,-256
	adds loc2=96*16-96,in0
	;;
	stf.spill.nta [loc0]=f43,-256
	stf.spill.nta [loc1]=f35,-256
	adds loc3=96*16-96-128,in0
	;;
	stf.spill.nta [loc2]=f122,-256
	stf.spill.nta [loc3]=f114,-256
	;;
	stf.spill.nta [loc2]=f106,-256
	stf.spill.nta [loc3]=f98,-256
	;;
	stf.spill.nta [loc2]=f90,-256
	stf.spill.nta [loc3]=f82,-256
	;;
	stf.spill.nta [loc2]=f74,-256
	stf.spill.nta [loc3]=f66,-256
	;;
	stf.spill.nta [loc2]=f58,-256
	stf.spill.nta [loc3]=f50,-256
	adds loc0=96*16-112,in0
	;;
	stf.spill.nta [loc2]=f42,-256
	stf.spill.nta [loc3]=f34,-256
	adds loc1=96*16-112-128,in0
	;;
	stf.spill.nta [loc0]=f121,-256
	stf.spill.nta [loc1]=f113,-256
	;;
	stf.spill.nta [loc0]=f105,-256
	stf.spill.nta [loc1]=f97,-256
	;;
	stf.spill.nta [loc0]=f89,-256
	stf.spill.nta [loc1]=f81,-256
	;;
	stf.spill.nta [loc0]=f73,-256
	stf.spill.nta [loc1]=f65,-256
	;;
	stf.spill.nta [loc0]=f57,-256
	stf.spill.nta [loc1]=f49,-256
	adds loc2=96*16-128,in0
	;;
	stf.spill.nta [loc0]=f41,-256
	stf.spill.nta [loc1]=f33,-256
	adds loc3=96*16-128-128,in0
	;;
	stf.spill.nta [loc2]=f120,-256
	stf.spill.nta [loc3]=f112,-256
	;;
	stf.spill.nta [loc2]=f104,-256
	stf.spill.nta [loc3]=f96,-256
	;;
	stf.spill.nta [loc2]=f88,-256
	stf.spill.nta [loc3]=f80,-256
	;;
	stf.spill.nta [loc2]=f72,-256
	stf.spill.nta [loc3]=f64,-256
	;;
	stf.spill.nta [loc2]=f56,-256
	stf.spill.nta [loc3]=f48,-256
	;;
	stf.spill.nta [loc2]=f40
	stf.spill.nta [loc3]=f32
	br.ret.sptk.many rp
END(__ia64_save_fpu)

GLOBAL_ENTRY(__ia64_load_fpu)
	alloc r2=ar.pfs,1,2,0,0
	adds r3=128,in0
	adds r14=256,in0
	adds r15=384,in0
	mov loc0=512
	mov loc1=-1024+16
	;;
	ldf.fill.nta f32=[in0],loc0
	ldf.fill.nta f40=[ r3],loc0
	ldf.fill.nta f48=[r14],loc0
	ldf.fill.nta f56=[r15],loc0
	;;
	ldf.fill.nta f64=[in0],loc0
	ldf.fill.nta f72=[ r3],loc0
	ldf.fill.nta f80=[r14],loc0
	ldf.fill.nta f88=[r15],loc0
	;;
	ldf.fill.nta f96=[in0],loc1
	ldf.fill.nta f104=[ r3],loc1
	ldf.fill.nta f112=[r14],loc1
	ldf.fill.nta f120=[r15],loc1
	;;
	ldf.fill.nta f33=[in0],loc0
	ldf.fill.nta f41=[ r3],loc0
	ldf.fill.nta f49=[r14],loc0
	ldf.fill.nta f57=[r15],loc0
	;;
	ldf.fill.nta f65=[in0],loc0
	ldf.fill.nta f73=[ r3],loc0
	ldf.fill.nta f81=[r14],loc0
	ldf.fill.nta f89=[r15],loc0
	;;
	ldf.fill.nta f97=[in0],loc1
	ldf.fill.nta f105=[ r3],loc1
	ldf.fill.nta f113=[r14],loc1
	ldf.fill.nta f121=[r15],loc1
	;;
	ldf.fill.nta f34=[in0],loc0
	ldf.fill.nta f42=[ r3],loc0
	ldf.fill.nta f50=[r14],loc0
	ldf.fill.nta f58=[r15],loc0
	;;
	ldf.fill.nta f66=[in0],loc0
	ldf.fill.nta f74=[ r3],loc0
	ldf.fill.nta f82=[r14],loc0
	ldf.fill.nta f90=[r15],loc0
	;;
	ldf.fill.nta f98=[in0],loc1
	ldf.fill.nta f106=[ r3],loc1
	ldf.fill.nta f114=[r14],loc1
	ldf.fill.nta f122=[r15],loc1
	;;
	ldf.fill.nta f35=[in0],loc0
	ldf.fill.nta f43=[ r3],loc0
	ldf.fill.nta f51=[r14],loc0
	ldf.fill.nta f59=[r15],loc0
	;;
	ldf.fill.nta f67=[in0],loc0
	ldf.fill.nta f75=[ r3],loc0
	ldf.fill.nta f83=[r14],loc0
	ldf.fill.nta f91=[r15],loc0
	;;
	ldf.fill.nta f99=[in0],loc1
	ldf.fill.nta f107=[ r3],loc1
	ldf.fill.nta f115=[r14],loc1
	ldf.fill.nta f123=[r15],loc1
	;;
	ldf.fill.nta f36=[in0],loc0
	ldf.fill.nta f44=[ r3],loc0
	ldf.fill.nta f52=[r14],loc0
	ldf.fill.nta f60=[r15],loc0
	;;
	ldf.fill.nta f68=[in0],loc0
	ldf.fill.nta f76=[ r3],loc0
	ldf.fill.nta f84=[r14],loc0
	ldf.fill.nta f92=[r15],loc0
	;;
	ldf.fill.nta f100=[in0],loc1
	ldf.fill.nta f108=[ r3],loc1
	ldf.fill.nta f116=[r14],loc1
	ldf.fill.nta f124=[r15],loc1
	;;
	ldf.fill.nta f37=[in0],loc0
	ldf.fill.nta f45=[ r3],loc0
	ldf.fill.nta f53=[r14],loc0
	ldf.fill.nta f61=[r15],loc0
	;;
	ldf.fill.nta f69=[in0],loc0
	ldf.fill.nta f77=[ r3],loc0
	ldf.fill.nta f85=[r14],loc0
	ldf.fill.nta f93=[r15],loc0
	;;
	ldf.fill.nta f101=[in0],loc1
	ldf.fill.nta f109=[ r3],loc1
	ldf.fill.nta f117=[r14],loc1
	ldf.fill.nta f125=[r15],loc1
	;;
	ldf.fill.nta f38 =[in0],loc0
	ldf.fill.nta f46 =[ r3],loc0
	ldf.fill.nta f54 =[r14],loc0
	ldf.fill.nta f62 =[r15],loc0
	;;
	ldf.fill.nta f70 =[in0],loc0
	ldf.fill.nta f78 =[ r3],loc0
	ldf.fill.nta f86 =[r14],loc0
	ldf.fill.nta f94 =[r15],loc0
	;;
	ldf.fill.nta f102=[in0],loc1
	ldf.fill.nta f110=[ r3],loc1
	ldf.fill.nta f118=[r14],loc1
	ldf.fill.nta f126=[r15],loc1
	;;
	ldf.fill.nta f39 =[in0],loc0
	ldf.fill.nta f47 =[ r3],loc0
	ldf.fill.nta f55 =[r14],loc0
	ldf.fill.nta f63 =[r15],loc0
	;;
	ldf.fill.nta f71 =[in0],loc0
	ldf.fill.nta f79 =[ r3],loc0
	ldf.fill.nta f87 =[r14],loc0
	ldf.fill.nta f95 =[r15],loc0
	;;
	ldf.fill.nta f103=[in0]
	ldf.fill.nta f111=[ r3]
	ldf.fill.nta f119=[r14]
	ldf.fill.nta f127=[r15]
	br.ret.sptk.many rp
END(__ia64_load_fpu)

GLOBAL_ENTRY(__ia64_init_fpu)
	stf.spill [sp]=f0		// M3
	mov	 f32=f0			// F
	nop.b	 0

	ldfps	 f33,f34=[sp]		// M0
	ldfps	 f35,f36=[sp]		// M1
	mov      f37=f0			// F
	;;

	setf.s	 f38=r0			// M2
	setf.s	 f39=r0			// M3
	mov      f40=f0			// F

	ldfps	 f41,f42=[sp]		// M0
	ldfps	 f43,f44=[sp]		// M1
	mov      f45=f0			// F

	setf.s	 f46=r0			// M2
	setf.s	 f47=r0			// M3
	mov      f48=f0			// F

	ldfps	 f49,f50=[sp]		// M0
	ldfps	 f51,f52=[sp]		// M1
	mov      f53=f0			// F

	setf.s	 f54=r0			// M2
	setf.s	 f55=r0			// M3
	mov      f56=f0			// F

	ldfps	 f57,f58=[sp]		// M0
	ldfps	 f59,f60=[sp]		// M1
	mov      f61=f0			// F

	setf.s	 f62=r0			// M2
	setf.s	 f63=r0			// M3
	mov      f64=f0			// F

	ldfps	 f65,f66=[sp]		// M0
	ldfps	 f67,f68=[sp]		// M1
	mov      f69=f0			// F

	setf.s	 f70=r0			// M2
	setf.s	 f71=r0			// M3
	mov      f72=f0			// F

	ldfps	 f73,f74=[sp]		// M0
	ldfps	 f75,f76=[sp]		// M1
	mov      f77=f0			// F

	setf.s	 f78=r0			// M2
	setf.s	 f79=r0			// M3
	mov      f80=f0			// F

	ldfps	 f81,f82=[sp]		// M0
	ldfps	 f83,f84=[sp]		// M1
	mov      f85=f0			// F

	setf.s	 f86=r0			// M2
	setf.s	 f87=r0			// M3
	mov      f88=f0			// F

	/*
	 * When the instructions are cached, it would be faster to initialize
	 * the remaining registers with simply mov instructions (F-unit).
	 * This gets the time down to ~29 cycles.  However, this would use up
	 * 33 bundles, whereas continuing with the above pattern yields
	 * 10 bundles and ~30 cycles.
	 */

	ldfps	 f89,f90=[sp]		// M0
	ldfps	 f91,f92=[sp]		// M1
	mov      f93=f0			// F

	setf.s	 f94=r0			// M2
	setf.s	 f95=r0			// M3
	mov      f96=f0			// F

	ldfps	 f97,f98=[sp]		// M0
	ldfps	 f99,f100=[sp]		// M1
	mov      f101=f0		// F

	setf.s	 f102=r0		// M2
	setf.s	 f103=r0		// M3
	mov      f104=f0		// F

	ldfps	 f105,f106=[sp]		// M0
	ldfps	 f107,f108=[sp]		// M1
	mov      f109=f0		// F

	setf.s	 f110=r0		// M2
	setf.s	 f111=r0		// M3
	mov      f112=f0		// F

	ldfps	 f113,f114=[sp]		// M0
	ldfps	 f115,f116=[sp]		// M1
	mov      f117=f0		// F

	setf.s	 f118=r0		// M2
	setf.s	 f119=r0		// M3
	mov      f120=f0		// F

	ldfps	 f121,f122=[sp]		// M0
	ldfps	 f123,f124=[sp]		// M1
	mov      f125=f0		// F

	setf.s	 f126=r0		// M2
	setf.s	 f127=r0		// M3
	br.ret.sptk.many rp		// F
END(__ia64_init_fpu)

/*
 * Switch execution mode from virtual to physical
 *
 * Inputs:
 *	r16 = new psr to establish
 * Output:
 *	r19 = old virtual address of ar.bsp
 *	r20 = old virtual address of sp
 *
 * Note: RSE must already be in enforced lazy mode
 */
GLOBAL_ENTRY(ia64_switch_mode_phys)
 {
	rsm psr.i | psr.ic		// disable interrupts and interrupt collection
	mov r15=ip
 }
	;;
 {
	flushrs				// must be first insn in group
	srlz.i
 }
	;;
	mov cr.ipsr=r16			// set new PSR
	add r3=1f-ia64_switch_mode_phys,r15

	mov r19=ar.bsp
	mov r20=sp
	mov r14=rp			// get return address into a general register
	;;

	// going to physical mode, use tpa to translate virt->phys
	tpa r17=r19
	tpa r3=r3
	tpa sp=sp
	tpa r14=r14
	;;

	mov r18=ar.rnat			// save ar.rnat
	mov ar.bspstore=r17		// this steps on ar.rnat
	mov cr.iip=r3
	mov cr.ifs=r0
	;;
	mov ar.rnat=r18			// restore ar.rnat
	rfi				// must be last insn in group
	;;
1:	mov rp=r14
	br.ret.sptk.many rp
END(ia64_switch_mode_phys)

/*
 * Switch execution mode from physical to virtual
 *
 * Inputs:
 *	r16 = new psr to establish
 *	r19 = new bspstore to establish
 *	r20 = new sp to establish
 *
 * Note: RSE must already be in enforced lazy mode
 */
GLOBAL_ENTRY(ia64_switch_mode_virt)
 {
	rsm psr.i | psr.ic		// disable interrupts and interrupt collection
	mov r15=ip
 }
	;;
 {
	flushrs				// must be first insn in group
	srlz.i
 }
	;;
	mov cr.ipsr=r16			// set new PSR
	add r3=1f-ia64_switch_mode_virt,r15

	mov r14=rp			// get return address into a general register
	;;

	// going to virtual
	//   - for code addresses, set upper bits of addr to KERNEL_START
	//   - for stack addresses, copy from input argument
	movl r18=KERNEL_START
	dep r3=0,r3,KERNEL_TR_PAGE_SHIFT,64-KERNEL_TR_PAGE_SHIFT
	dep r14=0,r14,KERNEL_TR_PAGE_SHIFT,64-KERNEL_TR_PAGE_SHIFT
	mov sp=r20
	;;
	or r3=r3,r18
	or r14=r14,r18
	;;

	mov r18=ar.rnat			// save ar.rnat
	mov ar.bspstore=r19		// this steps on ar.rnat
	mov cr.iip=r3
	mov cr.ifs=r0
	;;
	mov ar.rnat=r18			// restore ar.rnat
	rfi				// must be last insn in group
	;;
1:	mov rp=r14
	br.ret.sptk.many rp
END(ia64_switch_mode_virt)

GLOBAL_ENTRY(ia64_delay_loop)
	.prologue
{	nop 0			// work around GAS unwind info generation bug...
	.save ar.lc,r2
	mov r2=ar.lc
	.body
	;;
	mov ar.lc=r32
}
	;;
	// force loop to be 32-byte aligned (GAS bug means we cannot use .align
	// inside function body without corrupting unwind info).
{	nop 0 }
1:	br.cloop.sptk.few 1b
	;;
	mov ar.lc=r2
	br.ret.sptk.many rp
END(ia64_delay_loop)

/*
 * Return a CPU-local timestamp in nano-seconds.  This timestamp is
 * NOT synchronized across CPUs its return value must never be
 * compared against the values returned on another CPU.  The usage in
 * kernel/sched.c ensures that.
 *
 * The return-value of sched_clock() is NOT supposed to wrap-around.
 * If it did, it would cause some scheduling hiccups (at the worst).
 * Fortunately, with a 64-bit cycle-counter ticking at 100GHz, even
 * that would happen only once every 5+ years.
 *
 * The code below basically calculates:
 *
 *   (ia64_get_itc() * local_cpu_data->nsec_per_cyc) >> IA64_NSEC_PER_CYC_SHIFT
 *
 * except that the multiplication and the shift are done with 128-bit
 * intermediate precision so that we can produce a full 64-bit result.
 */
GLOBAL_ENTRY(sched_clock)
	addl r8=THIS_CPU(cpu_info) + IA64_CPUINFO_NSEC_PER_CYC_OFFSET,r0
	mov.m r9=ar.itc		// fetch cycle-counter				(35 cyc)
	;;
	ldf8 f8=[r8]
	;;
	setf.sig f9=r9		// certain to stall, so issue it _after_ ldf8...
	;;
	xmpy.lu f10=f9,f8	// calculate low 64 bits of 128-bit product	(4 cyc)
	xmpy.hu f11=f9,f8	// calculate high 64 bits of 128-bit product
	;;
	getf.sig r8=f10		//						(5 cyc)
	getf.sig r9=f11
	;;
	shrp r8=r9,r8,IA64_NSEC_PER_CYC_SHIFT
	br.ret.sptk.many rp
END(sched_clock)

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
GLOBAL_ENTRY(cycle_to_cputime)
	alloc r16=ar.pfs,1,0,0,0
	addl r8=THIS_CPU(cpu_info) + IA64_CPUINFO_NSEC_PER_CYC_OFFSET,r0
	;;
	ldf8 f8=[r8]
	;;
	setf.sig f9=r32
	;;
	xmpy.lu f10=f9,f8	// calculate low 64 bits of 128-bit product	(4 cyc)
	xmpy.hu f11=f9,f8	// calculate high 64 bits of 128-bit product
	;;
	getf.sig r8=f10		//						(5 cyc)
	getf.sig r9=f11
	;;
	shrp r8=r9,r8,IA64_NSEC_PER_CYC_SHIFT
	br.ret.sptk.many rp
END(cycle_to_cputime)
#endif /* CONFIG_VIRT_CPU_ACCOUNTING */

GLOBAL_ENTRY(start_kernel_thread)
	.prologue
	.save rp, r0				// this is the end of the call-chain
	.body
	alloc r2 = ar.pfs, 0, 0, 2, 0
	mov out0 = r9
	mov out1 = r11;;
	br.call.sptk.many rp = kernel_thread_helper;;
	mov out0 = r8
	br.call.sptk.many rp = sys_exit;;
1:	br.sptk.few 1b				// not reached
END(start_kernel_thread)

#ifdef CONFIG_IA64_BRL_EMU

/*
 *  Assembly routines used by brl_emu.c to set preserved register state.
 */

#define SET_REG(reg)				\
 GLOBAL_ENTRY(ia64_set_##reg);			\
	alloc r16=ar.pfs,1,0,0,0;		\
	mov reg=r32;				\
	;;					\
	br.ret.sptk.many rp;			\
 END(ia64_set_##reg)

SET_REG(b1);
SET_REG(b2);
SET_REG(b3);
SET_REG(b4);
SET_REG(b5);

#endif /* CONFIG_IA64_BRL_EMU */

#ifdef CONFIG_SMP
	/*
	 * This routine handles spinlock contention.  It uses a non-standard calling
	 * convention to avoid converting leaf routines into interior routines.  Because
	 * of this special convention, there are several restrictions:
	 *
	 * - do not use gp relative variables, this code is called from the kernel
	 *   and from modules, r1 is undefined.
	 * - do not use stacked registers, the caller owns them.
	 * - do not use the scratch stack space, the caller owns it.
	 * - do not use any registers other than the ones listed below
	 *
	 * Inputs:
	 *   ar.pfs - saved CFM of caller
	 *   ar.ccv - 0 (and available for use)
	 *   r27    - flags from spin_lock_irqsave or 0.  Must be preserved.
	 *   r28    - available for use.
	 *   r29    - available for use.
	 *   r30    - available for use.
	 *   r31    - address of lock, available for use.
	 *   b6     - return address
	 *   p14    - available for use.
	 *   p15    - used to track flag status.
	 *
	 * If you patch this code to use more registers, do not forget to update
	 * the clobber lists for spin_lock() in arch/ia64/include/asm/spinlock.h.
	 */

#if (__GNUC__ == 3 && __GNUC_MINOR__ < 3)

GLOBAL_ENTRY(ia64_spinlock_contention_pre3_4)
	.prologue
	.save ar.pfs, r0	// this code effectively has a zero frame size
	.save rp, r28
	.body
	nop 0
	tbit.nz p15,p0=r27,IA64_PSR_I_BIT
	.restore sp		// pop existing prologue after next insn
	mov b6 = r28
	.prologue
	.save ar.pfs, r0
	.altrp b6
	.body
	;;
(p15)	ssm psr.i		// reenable interrupts if they were on
				// DavidM says that srlz.d is slow and is not required in this case
.wait:
	// exponential backoff, kdb, lockmeter etc. go in here
	hint @pause
	ld4 r30=[r31]		// don't use ld4.bias; if it's contended, we won't write the word
	nop 0
	;;
	cmp4.ne p14,p0=r30,r0
(p14)	br.cond.sptk.few .wait
(p15)	rsm psr.i		// disable interrupts if we reenabled them
	br.cond.sptk.few b6	// lock is now free, try to acquire
	.global ia64_spinlock_contention_pre3_4_end	// for kernprof
ia64_spinlock_contention_pre3_4_end:
END(ia64_spinlock_contention_pre3_4)

#else

GLOBAL_ENTRY(ia64_spinlock_contention)
	.prologue
	.altrp b6
	.body
	tbit.nz p15,p0=r27,IA64_PSR_I_BIT
	;;
.wait:
(p15)	ssm psr.i		// reenable interrupts if they were on
				// DavidM says that srlz.d is slow and is not required in this case
.wait2:
	// exponential backoff, kdb, lockmeter etc. go in here
	hint @pause
	ld4 r30=[r31]		// don't use ld4.bias; if it's contended, we won't write the word
	;;
	cmp4.ne p14,p0=r30,r0
	mov r30 = 1
(p14)	br.cond.sptk.few .wait2
(p15)	rsm psr.i		// disable interrupts if we reenabled them
	;;
	cmpxchg4.acq r30=[r31], r30, ar.ccv
	;;
	cmp4.ne p14,p0=r0,r30
(p14)	br.cond.sptk.few .wait

	br.ret.sptk.many b6	// lock is now taken
END(ia64_spinlock_contention)

#endif

#ifdef CONFIG_HOTPLUG_CPU
GLOBAL_ENTRY(ia64_jump_to_sal)
	alloc r16=ar.pfs,1,0,0,0;;
	rsm psr.i  | psr.ic
{
	flushrs
	srlz.i
}
	tpa r25=in0
	movl r18=tlb_purge_done;;
	DATA_VA_TO_PA(r18);;
	mov b1=r18 	// Return location
	movl r18=ia64_do_tlb_purge;;
	DATA_VA_TO_PA(r18);;
	mov b2=r18 	// doing tlb_flush work
	mov ar.rsc=0  // Put RSE  in enforced lazy, LE mode
	movl r17=1f;;
	DATA_VA_TO_PA(r17);;
	mov cr.iip=r17
	movl r16=SAL_PSR_BITS_TO_SET;;
	mov cr.ipsr=r16
	mov cr.ifs=r0;;
	rfi;;
1:
	/*
	 * Invalidate all TLB data/inst
	 */
	br.sptk.many b2;; // jump to tlb purge code

tlb_purge_done:
	RESTORE_REGION_REGS(r25, r17,r18,r19);;
	RESTORE_REG(b0, r25, r17);;
	RESTORE_REG(b1, r25, r17);;
	RESTORE_REG(b2, r25, r17);;
	RESTORE_REG(b3, r25, r17);;
	RESTORE_REG(b4, r25, r17);;
	RESTORE_REG(b5, r25, r17);;
	ld8 r1=[r25],0x08;;
	ld8 r12=[r25],0x08;;
	ld8 r13=[r25],0x08;;
	RESTORE_REG(ar.fpsr, r25, r17);;
	RESTORE_REG(ar.pfs, r25, r17);;
	RESTORE_REG(ar.rnat, r25, r17);;
	RESTORE_REG(ar.unat, r25, r17);;
	RESTORE_REG(ar.bspstore, r25, r17);;
	RESTORE_REG(cr.dcr, r25, r17);;
	RESTORE_REG(cr.iva, r25, r17);;
	RESTORE_REG(cr.pta, r25, r17);;
	srlz.d;;	// required not to violate RAW dependency
	RESTORE_REG(cr.itv, r25, r17);;
	RESTORE_REG(cr.pmv, r25, r17);;
	RESTORE_REG(cr.cmcv, r25, r17);;
	RESTORE_REG(cr.lrr0, r25, r17);;
	RESTORE_REG(cr.lrr1, r25, r17);;
	ld8 r4=[r25],0x08;;
	ld8 r5=[r25],0x08;;
	ld8 r6=[r25],0x08;;
	ld8 r7=[r25],0x08;;
	ld8 r17=[r25],0x08;;
	mov pr=r17,-1;;
	RESTORE_REG(ar.lc, r25, r17);;
	/*
	 * Now Restore floating point regs
	 */
	ldf.fill.nta f2=[r25],16;;
	ldf.fill.nta f3=[r25],16;;
	ldf.fill.nta f4=[r25],16;;
	ldf.fill.nta f5=[r25],16;;
	ldf.fill.nta f16=[r25],16;;
	ldf.fill.nta f17=[r25],16;;
	ldf.fill.nta f18=[r25],16;;
	ldf.fill.nta f19=[r25],16;;
	ldf.fill.nta f20=[r25],16;;
	ldf.fill.nta f21=[r25],16;;
	ldf.fill.nta f22=[r25],16;;
	ldf.fill.nta f23=[r25],16;;
	ldf.fill.nta f24=[r25],16;;
	ldf.fill.nta f25=[r25],16;;
	ldf.fill.nta f26=[r25],16;;
	ldf.fill.nta f27=[r25],16;;
	ldf.fill.nta f28=[r25],16;;
	ldf.fill.nta f29=[r25],16;;
	ldf.fill.nta f30=[r25],16;;
	ldf.fill.nta f31=[r25],16;;

	/*
	 * Now that we have done all the register restores
	 * we are now ready for the big DIVE to SAL Land
	 */
	ssm psr.ic;;
	srlz.d;;
	br.ret.sptk.many b0;;
END(ia64_jump_to_sal)
#endif /* CONFIG_HOTPLUG_CPU */

#endif /* CONFIG_SMP */
