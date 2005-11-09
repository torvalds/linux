/*
 *  arch/ppc/kernel/misc.S
 *
 *  
 *
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com) 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>

	.text

/*
 * Returns (address we were linked at) - (address we are running at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	LOADADDR(r4,1b)
	sub	r3,r4,r3
	mtlr	r0
	blr

_GLOBAL(get_msr)
	mfmsr	r3
	blr

_GLOBAL(get_dar)
	mfdar	r3
	blr

_GLOBAL(get_srr0)
	mfsrr0  r3
	blr

_GLOBAL(get_srr1)
	mfsrr1  r3
	blr
	
_GLOBAL(get_sp)
	mr	r3,r1
	blr

#ifdef CONFIG_IRQSTACKS
_GLOBAL(call_do_softirq)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,THREAD_SIZE-112(r3)
	mr	r1,r3
	bl	.__do_softirq
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr

_GLOBAL(call_handle_IRQ_event)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,THREAD_SIZE-112(r6)
	mr	r1,r6
	bl	.handle_IRQ_event
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr
#endif /* CONFIG_IRQSTACKS */

	/*
 * To be called by C code which needs to do some operations with MMU
 * disabled. Note that interrupts have to be disabled by the caller
 * prior to calling us. The code called _MUST_ be in the RMO of course
 * and part of the linear mapping as we don't attempt to translate the
 * stack pointer at all. The function is called with the stack switched
 * to this CPU emergency stack
 *
 * prototype is void *call_with_mmu_off(void *func, void *data);
 *
 * the called function is expected to be of the form
 *
 * void *called(void *data); 
 */
_GLOBAL(call_with_mmu_off)
	mflr	r0			/* get link, save it on stackframe */
	std	r0,16(r1)
	mr	r1,r5			/* save old stack ptr */
	ld	r1,PACAEMERGSP(r13)	/* get emerg. stack */
	subi	r1,r1,STACK_FRAME_OVERHEAD
	std	r0,16(r1)		/* save link on emerg. stack */
	std	r5,0(r1)		/* save old stack ptr in backchain */
	ld	r3,0(r3)		/* get to real function ptr (assume same TOC) */
	bl	2f			/* we need LR to return, continue at label 2 */

	ld	r0,16(r1)		/* we return here from the call, get LR and */
	ld	r1,0(r1)		/* .. old stack ptr */
	mtspr	SPRN_SRR0,r0		/* and get back to virtual mode with these */
	mfmsr	r4
	ori	r4,r4,MSR_IR|MSR_DR
	mtspr	SPRN_SRR1,r4
	rfid

2:	mtspr	SPRN_SRR0,r3		/* coming from above, enter real mode */
	mr	r3,r4			/* get parameter */
	mfmsr	r0
	ori	r0,r0,MSR_IR|MSR_DR
	xori	r0,r0,MSR_IR|MSR_DR
	mtspr	SPRN_SRR1,r0
	rfid


	.section	".toc","aw"
PPC64_CACHES:
	.tc		ppc64_caches[TC],ppc64_caches
	.section	".text"

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 *
 *   flush all bytes from start through stop-1 inclusive
 */

_KPROBE(__flush_icache_range)

/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 * and in some cases i-cache and d-cache line sizes differ from
 * each other.
 */
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1LINESIZE(r10)/* Get cache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)	/* Get log-2 of cache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	1b
	sync

/* Now invalidate the instruction cache */
	
	lwz	r7,ICACHEL1LINESIZE(r10)	/* Get Icache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lwz	r9,ICACHEL1LOGLINESIZE(r10)	/* Get log-2 of Icache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
2:	icbi	0,r6
	add	r6,r6,r7
	bdnz	2b
	isync
	blr
	.previous .text
/*
 * Like above, but only do the D-cache.
 *
 * flush_dcache_range(unsigned long start, unsigned long stop)
 *
 *    flush all bytes from start to stop-1 inclusive
 */
_GLOBAL(flush_dcache_range)

/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 */
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1LINESIZE(r10)	/* Get dcache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)	/* Get log-2 of dcache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
0:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	0b
	sync
	blr

/*
 * Like above, but works on non-mapped physical addresses.
 * Use only for non-LPAR setups ! It also assumes real mode
 * is cacheable. Used for flushing out the DART before using
 * it as uncacheable memory 
 *
 * flush_dcache_phys_range(unsigned long start, unsigned long stop)
 *
 *    flush all bytes from start to stop-1 inclusive
 */
_GLOBAL(flush_dcache_phys_range)
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1LINESIZE(r10)	/* Get dcache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)	/* Get log-2 of dcache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mfmsr	r5			/* Disable MMU Data Relocation */
	ori	r0,r5,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsr	r0
	sync
	isync
	mtctr	r8
0:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	0b
	sync
	isync
	mtmsr	r5			/* Re-enable MMU Data Relocation */
	sync
	isync
	blr

_GLOBAL(flush_inval_dcache_range)
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1LINESIZE(r10)	/* Get dcache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)/* Get log-2 of dcache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	sync
	isync
	mtctr	r8
0:	dcbf	0,r6
	add	r6,r6,r7
	bdnz	0b
	sync
	isync
	blr


/*
 * Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *
 *	void __flush_dcache_icache(void *page)
 */
_GLOBAL(__flush_dcache_icache)
/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 */

/* Flush the dcache */
 	ld	r7,PPC64_CACHES@toc(r2)
	clrrdi	r3,r3,PAGE_SHIFT           	    /* Page align */
	lwz	r4,DCACHEL1LINESPERPAGE(r7)	/* Get # dcache lines per page */
	lwz	r5,DCACHEL1LINESIZE(r7)		/* Get dcache line size */
	mr	r6,r3
	mtctr	r4
0:	dcbst	0,r6
	add	r6,r6,r5
	bdnz	0b
	sync

/* Now invalidate the icache */	

	lwz	r4,ICACHEL1LINESPERPAGE(r7)	/* Get # icache lines per page */
	lwz	r5,ICACHEL1LINESIZE(r7)		/* Get icache line size */
	mtctr	r4
1:	icbi	0,r3
	add	r3,r3,r5
	bdnz	1b
	isync
	blr
	
/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
	eieio
	stbu	r5,1(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbzu	r5,1(r4)
	stb	r5,0(r3)
	bdnz	00b
	sync
	blr	

_GLOBAL(_insw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	sthbrx	r5,0,r3	
	bdnz	00b
	sync
	blr	

_GLOBAL(_insl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3
	bdnz	00b
	sync
	blr	

/* _GLOBAL(ide_insw) now in drivers/ide/ide-iops.c */
_GLOBAL(_insw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

/* _GLOBAL(ide_outsw) now in drivers/ide/ide-iops.c */
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	sth	r5,0(r3)
	bdnz	00b
	sync
	blr	

_GLOBAL(_insl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stw	r5,0(r3)
	bdnz	00b
	sync
	blr	

/*
 * identify_cpu and calls setup_cpu
 * In:	r3 = base of the cpu_specs array
 *	r4 = address of cur_cpu_spec
 *	r5 = relocation offset
 */
_GLOBAL(identify_cpu)
	mfpvr	r7
1:
	lwz	r8,CPU_SPEC_PVR_MASK(r3)
	and	r8,r8,r7
	lwz	r9,CPU_SPEC_PVR_VALUE(r3)
	cmplw	0,r9,r8
	beq	1f
	addi	r3,r3,CPU_SPEC_ENTRY_SIZE
	b	1b
1:
	add	r0,r3,r5
	std	r0,0(r4)
	ld	r4,CPU_SPEC_SETUP(r3)
	sub	r4,r4,r5
	ld	r4,0(r4)
	sub	r4,r4,r5
	mtctr	r4
	/* Calling convention for cpu setup is r3=offset, r4=cur_cpu_spec */
	mr	r4,r3
	mr	r3,r5
	bctr

/*
 * do_cpu_ftr_fixups - goes through the list of CPU feature fixups
 * and writes nop's over sections of code that don't apply for this cpu.
 * r3 = data offset (not changed)
 */
_GLOBAL(do_cpu_ftr_fixups)
	/* Get CPU 0 features */
	LOADADDR(r6,cur_cpu_spec)
	sub	r6,r6,r3
	ld	r4,0(r6)
	sub	r4,r4,r3
	ld	r4,CPU_SPEC_FEATURES(r4)
	/* Get the fixup table */
	LOADADDR(r6,__start___ftr_fixup)
	sub	r6,r6,r3
	LOADADDR(r7,__stop___ftr_fixup)
	sub	r7,r7,r3
	/* Do the fixup */
1:	cmpld	r6,r7
	bgelr
	addi	r6,r6,32
	ld	r8,-32(r6)	/* mask */
	and	r8,r8,r4
	ld	r9,-24(r6)	/* value */
	cmpld	r8,r9
	beq	1b
	ld	r8,-16(r6)	/* section begin */
	ld	r9,-8(r6)	/* section end */
	subf.	r9,r8,r9
	beq	1b
	/* write nops over the section of code */
	/* todo: if large section, add a branch at the start of it */
	srwi	r9,r9,2
	mtctr	r9
	sub	r8,r8,r3
	lis	r0,0x60000000@h	/* nop */
3:	stw	r0,0(r8)
	andi.	r10,r4,CPU_FTR_SPLIT_ID_CACHE@l
	beq	2f
	dcbst	0,r8		/* suboptimal, but simpler */
	sync
	icbi	0,r8
2:	addi	r8,r8,4
	bdnz	3b
	sync			/* additional sync needed on g4 */
	isync
	b	1b

#if defined(CONFIG_PPC_PMAC) || defined(CONFIG_PPC_MAPLE)
/*
 * Do an IO access in real mode
 */
_GLOBAL(real_readb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsrd	r0
	sync
	isync
	mfspr	r6,SPRN_HID4
	rldicl	r5,r6,32,0
	ori	r5,r5,0x100
	rldicl	r5,r5,32,0
	sync
	mtspr	SPRN_HID4,r5
	isync
	slbia
	isync
	lbz	r3,0(r3)
	sync
	mtspr	SPRN_HID4,r6
	isync
	slbia
	isync
	mtmsrd	r7
	sync
	isync
	blr

/*
 * Do an IO access in real mode
 */
_GLOBAL(real_writeb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsrd	r0
	sync
	isync
	mfspr	r6,SPRN_HID4
	rldicl	r5,r6,32,0
	ori	r5,r5,0x100
	rldicl	r5,r5,32,0
	sync
	mtspr	SPRN_HID4,r5
	isync
	slbia
	isync
	stb	r3,0(r4)
	sync
	mtspr	SPRN_HID4,r6
	isync
	slbia
	isync
	mtmsrd	r7
	sync
	isync
	blr
#endif /* defined(CONFIG_PPC_PMAC) || defined(CONFIG_PPC_MAPLE) */

/*
 * SCOM access functions for 970 (FX only for now)
 *
 * unsigned long scom970_read(unsigned int address);
 * void scom970_write(unsigned int address, unsigned long value);
 *
 * The address passed in is the 24 bits register address. This code
 * is 970 specific and will not check the status bits, so you should
 * know what you are doing.
 */
_GLOBAL(scom970_read)
	/* interrupts off */
	mfmsr	r4
	ori	r0,r4,MSR_EE
	xori	r0,r0,MSR_EE
	mtmsrd	r0,1

	/* rotate 24 bits SCOM address 8 bits left and mask out it's low 8 bits
	 * (including parity). On current CPUs they must be 0'd,
	 * and finally or in RW bit
	 */
	rlwinm	r3,r3,8,0,15
	ori	r3,r3,0x8000

	/* do the actual scom read */
	sync
	mtspr	SPRN_SCOMC,r3
	isync
	mfspr	r3,SPRN_SCOMD
	isync
	mfspr	r0,SPRN_SCOMC
	isync

	/* XXX:	fixup result on some buggy 970's (ouch ! we lost a bit, bah
	 * that's the best we can do). Not implemented yet as we don't use
	 * the scom on any of the bogus CPUs yet, but may have to be done
	 * ultimately
	 */

	/* restore interrupts */
	mtmsrd	r4,1
	blr


_GLOBAL(scom970_write)
	/* interrupts off */
	mfmsr	r5
	ori	r0,r5,MSR_EE
	xori	r0,r0,MSR_EE
	mtmsrd	r0,1

	/* rotate 24 bits SCOM address 8 bits left and mask out it's low 8 bits
	 * (including parity). On current CPUs they must be 0'd.
	 */

	rlwinm	r3,r3,8,0,15

	sync
	mtspr	SPRN_SCOMD,r4      /* write data */
	isync
	mtspr	SPRN_SCOMC,r3      /* write command */
	isync
	mfspr	3,SPRN_SCOMC
	isync

	/* restore interrupts */
	mtmsrd	r5,1
	blr


/*
 * Create a kernel thread
 *   kernel_thread(fn, arg, flags)
 */
_GLOBAL(kernel_thread)
	std	r29,-24(r1)
	std	r30,-16(r1)
	stdu	r1,-STACK_FRAME_OVERHEAD(r1)
	mr	r29,r3
	mr	r30,r4
	ori	r3,r5,CLONE_VM	/* flags */
	oris	r3,r3,(CLONE_UNTRACED>>16)
	li	r4,0		/* new sp (unused) */
	li	r0,__NR_clone
	sc
	cmpdi	0,r3,0		/* parent or child? */
	bne	1f		/* return if parent */
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)
	ld	r2,8(r29)
	ld	r29,0(r29)
	mtlr	r29              /* fn addr in lr */
	mr	r3,r30	        /* load arg and call fn */
	blrl
	li	r0,__NR_exit	/* exit after child exits */
        li	r3,0
	sc
1:	addi	r1,r1,STACK_FRAME_OVERHEAD	
	ld	r29,-24(r1)
	ld	r30,-16(r1)
	blr

/*
 * disable_kernel_fp()
 * Disable the FPU.
 */
_GLOBAL(disable_kernel_fp)
	mfmsr	r3
	rldicl	r0,r3,(63-MSR_FP_LG),1
	rldicl	r3,r0,(MSR_FP_LG+1),0
	mtmsrd	r3			/* disable use of fpu now */
	isync
	blr

#ifdef CONFIG_ALTIVEC

#if 0 /* this has no callers for now */
/*
 * disable_kernel_altivec()
 * Disable the VMX.
 */
_GLOBAL(disable_kernel_altivec)
	mfmsr	r3
	rldicl	r0,r3,(63-MSR_VEC_LG),1
	rldicl	r3,r0,(MSR_VEC_LG+1),0
	mtmsrd	r3			/* disable use of VMX now */
	isync
	blr
#endif /* 0 */

/*
 * giveup_altivec(tsk)
 * Disable VMX for the task given as the argument,
 * and save the vector registers in its thread_struct.
 * Enables the VMX for use in the kernel on return.
 */
_GLOBAL(giveup_altivec)
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	mtmsrd	r5			/* enable use of VMX now */
	isync
	cmpdi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	ld	r5,PT_REGS(r3)
	cmpdi	0,r5,0
	SAVE_32VRS(0,r4,r3)
	mfvscr	vr0
	li	r4,THREAD_VSCR
	stvx	vr0,r4,r3
	beq	1f
	ld	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_VEC@h
	andc	r4,r4,r3		/* disable FP for previous task */
	std	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	ld	r4,last_task_used_altivec@got(r2)
	std	r5,0(r4)
#endif /* CONFIG_SMP */
	blr

#endif /* CONFIG_ALTIVEC */

_GLOBAL(__setup_cpu_power3)
	blr

_GLOBAL(execve)
	li	r0,__NR_execve
	sc
	bnslr
	neg	r3,r3
	blr

/* kexec_wait(phys_cpu)
 *
 * wait for the flag to change, indicating this kernel is going away but
 * the slave code for the next one is at addresses 0 to 100.
 *
 * This is used by all slaves.
 *
 * Physical (hardware) cpu id should be in r3.
 */
_GLOBAL(kexec_wait)
	bl	1f
1:	mflr	r5
	addi	r5,r5,kexec_flag-1b

99:	HMT_LOW
#ifdef CONFIG_KEXEC		/* use no memory without kexec */
	lwz	r4,0(r5)
	cmpwi	0,r4,0
	bnea	0x60
#endif
	b	99b

/* this can be in text because we won't change it until we are
 * running in real anyways
 */
kexec_flag:
	.long	0


#ifdef CONFIG_KEXEC

/* kexec_smp_wait(void)
 *
 * call with interrupts off
 * note: this is a terminal routine, it does not save lr
 *
 * get phys id from paca
 * set paca id to -1 to say we got here
 * switch to real mode
 * join other cpus in kexec_wait(phys_id)
 */
_GLOBAL(kexec_smp_wait)
	lhz	r3,PACAHWCPUID(r13)
	li	r4,-1
	sth	r4,PACAHWCPUID(r13)	/* let others know we left */
	bl	real_mode
	b	.kexec_wait

/*
 * switch to real mode (turn mmu off)
 * we use the early kernel trick that the hardware ignores bits
 * 0 and 1 (big endian) of the effective address in real mode
 *
 * don't overwrite r3 here, it is live for kexec_wait above.
 */
real_mode:	/* assume normal blr return */
1:	li	r9,MSR_RI
	li	r10,MSR_DR|MSR_IR
	mflr	r11		/* return address to SRR0 */
	mfmsr	r12
	andc	r9,r12,r9
	andc	r10,r12,r10

	mtmsrd	r9,1
	mtspr	SPRN_SRR1,r10
	mtspr	SPRN_SRR0,r11
	rfid


/*
 * kexec_sequence(newstack, start, image, control, clear_all())
 *
 * does the grungy work with stack switching and real mode switches
 * also does simple calls to other code
 */

_GLOBAL(kexec_sequence)
	mflr	r0
	std	r0,16(r1)

	/* switch stacks to newstack -- &kexec_stack.stack */
	stdu	r1,THREAD_SIZE-112(r3)
	mr	r1,r3

	li	r0,0
	std	r0,16(r1)

	/* save regs for local vars on new stack.
	 * yes, we won't go back, but ...
	 */
	std	r31,-8(r1)
	std	r30,-16(r1)
	std	r29,-24(r1)
	std	r28,-32(r1)
	std	r27,-40(r1)
	std	r26,-48(r1)
	std	r25,-56(r1)

	stdu	r1,-112-64(r1)

	/* save args into preserved regs */
	mr	r31,r3			/* newstack (both) */
	mr	r30,r4			/* start (real) */
	mr	r29,r5			/* image (virt) */
	mr	r28,r6			/* control, unused */
	mr	r27,r7			/* clear_all() fn desc */
	mr	r26,r8			/* spare */
	lhz	r25,PACAHWCPUID(r13)	/* get our phys cpu from paca */

	/* disable interrupts, we are overwriting kernel data next */
	mfmsr	r3
	rlwinm	r3,r3,0,17,15
	mtmsrd	r3,1

	/* copy dest pages, flush whole dest image */
	mr	r3,r29
	bl	.kexec_copy_flush	/* (image) */

	/* turn off mmu */
	bl	real_mode

	/* clear out hardware hash page table and tlb */
	ld	r5,0(r27)		/* deref function descriptor */
	mtctr	r5
	bctrl				/* ppc_md.hash_clear_all(void); */

/*
 *   kexec image calling is:
 *      the first 0x100 bytes of the entry point are copied to 0
 *
 *      all slaves branch to slave = 0x60 (absolute)
 *              slave(phys_cpu_id);
 *
 *      master goes to start = entry point
 *              start(phys_cpu_id, start, 0);
 *
 *
 *   a wrapper is needed to call existing kernels, here is an approximate
 *   description of one method:
 *
 * v2: (2.6.10)
 *   start will be near the boot_block (maybe 0x100 bytes before it?)
 *   it will have a 0x60, which will b to boot_block, where it will wait
 *   and 0 will store phys into struct boot-block and load r3 from there,
 *   copy kernel 0-0x100 and tell slaves to back down to 0x60 again
 *
 * v1: (2.6.9)
 *    boot block will have all cpus scanning device tree to see if they
 *    are the boot cpu ?????
 *    other device tree differences (prop sizes, va vs pa, etc)...
 */

	/* copy  0x100 bytes starting at start to 0 */
	li	r3,0
	mr	r4,r30
	li	r5,0x100
	li	r6,0
	bl	.copy_and_flush	/* (dest, src, copy limit, start offset) */
1:	/* assume normal blr return */

	/* release other cpus to the new kernel secondary start at 0x60 */
	mflr	r5
	li	r6,1
	stw	r6,kexec_flag-1b(5)
	mr	r3,r25	# my phys cpu
	mr	r4,r30	# start, aka phys mem offset
	mtlr	4
	li	r5,0
	blr	/* image->start(physid, image->start, 0); */
#endif /* CONFIG_KEXEC */
