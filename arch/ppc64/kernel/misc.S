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
#include <asm/offsets.h>
#include <asm/cputable.h>

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
		
#ifdef CONFIG_PPC_ISERIES
/* unsigned long local_save_flags(void) */
_GLOBAL(local_get_flags)
	lbz	r3,PACAPROCENABLED(r13)
	blr

/* unsigned long local_irq_disable(void) */
_GLOBAL(local_irq_disable)
	lbz	r3,PACAPROCENABLED(r13)
	li	r4,0
	stb	r4,PACAPROCENABLED(r13)
	blr			/* Done */

/* void local_irq_restore(unsigned long flags) */	
_GLOBAL(local_irq_restore)
	lbz	r5,PACAPROCENABLED(r13)
	 /* Check if things are setup the way we want _already_. */
	cmpw	0,r3,r5
	beqlr
	/* are we enabling interrupts? */
	cmpdi	0,r3,0
	stb	r3,PACAPROCENABLED(r13)
	beqlr
	/* Check pending interrupts */
	/*   A decrementer, IPI or PMC interrupt may have occurred
	 *   while we were in the hypervisor (which enables) */
	ld	r4,PACALPPACA+LPPACAANYINT(r13)
	cmpdi	r4,0
	beqlr

	/*
	 * Handle pending interrupts in interrupt context
	 */
	li	r0,0x5555
	sc
	blr
#endif /* CONFIG_PPC_ISERIES */

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

_GLOBAL(__flush_icache_range)

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
	clrrdi	r3,r3,12           	    /* Page align */
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


_GLOBAL(cvt_fd)
	lfd	0,0(r5)		/* load up fpscr value */
	mtfsf	0xff,0
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,0(r5)
	blr

_GLOBAL(cvt_df)
	lfd	0,0(r5)		/* load up fpscr value */
	mtfsf	0xff,0
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,0(r5)
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

/* Why isn't this a) automatic, b) written in 'C'? */	
	.balign 8
_GLOBAL(sys_call_table32)
	.llong .sys_restart_syscall	/* 0 */
	.llong .sys_exit
	.llong .ppc_fork
	.llong .sys_read
	.llong .sys_write
	.llong .sys32_open		/* 5 */
	.llong .sys_close
	.llong .sys32_waitpid
	.llong .sys32_creat
	.llong .sys_link
	.llong .sys_unlink      	/* 10 */
	.llong .sys32_execve
	.llong .sys_chdir
	.llong .compat_sys_time
	.llong .sys_mknod
	.llong .sys_chmod		/* 15 */
	.llong .sys_lchown
	.llong .sys_ni_syscall		/* old break syscall */
	.llong .sys_ni_syscall		/* old stat syscall */
	.llong .ppc32_lseek
	.llong .sys_getpid              /* 20 */
	.llong .compat_sys_mount
	.llong .sys_oldumount
	.llong .sys_setuid
	.llong .sys_getuid
	.llong .compat_sys_stime	/* 25 */
	.llong .sys32_ptrace
	.llong .sys_alarm
	.llong .sys_ni_syscall		/* old fstat syscall */
	.llong .sys32_pause
	.llong .compat_sys_utime		/* 30 */
	.llong .sys_ni_syscall		/* old stty syscall */
	.llong .sys_ni_syscall		/* old gtty syscall */
	.llong .sys32_access
	.llong .sys32_nice
	.llong .sys_ni_syscall		/* 35 - old ftime syscall */
	.llong .sys_sync
	.llong .sys32_kill
	.llong .sys_rename
	.llong .sys32_mkdir
	.llong .sys_rmdir		/* 40 */
	.llong .sys_dup
	.llong .sys_pipe
	.llong .compat_sys_times
	.llong .sys_ni_syscall		/* old prof syscall */
	.llong .sys_brk			/* 45 */
	.llong .sys_setgid
	.llong .sys_getgid
	.llong .sys_signal
	.llong .sys_geteuid
	.llong .sys_getegid		/* 50 */
	.llong .sys_acct
	.llong .sys_umount
	.llong .sys_ni_syscall		/* old lock syscall */
	.llong .compat_sys_ioctl
	.llong .compat_sys_fcntl		/* 55 */
	.llong .sys_ni_syscall		/* old mpx syscall */
	.llong .sys32_setpgid
	.llong .sys_ni_syscall		/* old ulimit syscall */
	.llong .sys32_olduname
	.llong .sys32_umask		/* 60 */
	.llong .sys_chroot
	.llong .sys_ustat
	.llong .sys_dup2
	.llong .sys_getppid
	.llong .sys_getpgrp	        /* 65 */
	.llong .sys_setsid
	.llong .sys32_sigaction
	.llong .sys_sgetmask
	.llong .sys32_ssetmask
	.llong .sys_setreuid	        /* 70 */
	.llong .sys_setregid
	.llong .ppc32_sigsuspend
	.llong .compat_sys_sigpending
	.llong .sys32_sethostname
	.llong .compat_sys_setrlimit	        /* 75 */
	.llong .compat_sys_old_getrlimit
	.llong .compat_sys_getrusage
	.llong .sys32_gettimeofday
	.llong .sys32_settimeofday
	.llong .sys32_getgroups	        /* 80 */
	.llong .sys32_setgroups
	.llong .sys_ni_syscall		/* old select syscall */
	.llong .sys_symlink
	.llong .sys_ni_syscall		/* old lstat syscall */
	.llong .sys32_readlink	        /* 85 */
	.llong .sys_uselib
	.llong .sys_swapon
	.llong .sys_reboot
	.llong .old32_readdir
	.llong .sys_mmap		/* 90 */
	.llong .sys_munmap
	.llong .sys_truncate
	.llong .sys_ftruncate
	.llong .sys_fchmod
	.llong .sys_fchown              /* 95 */
	.llong .sys32_getpriority
	.llong .sys32_setpriority
	.llong .sys_ni_syscall		/* old profil syscall */
	.llong .compat_sys_statfs
	.llong .compat_sys_fstatfs		/* 100 */
	.llong .sys_ni_syscall		/* old ioperm syscall */
	.llong .compat_sys_socketcall
	.llong .sys32_syslog
	.llong .compat_sys_setitimer
	.llong .compat_sys_getitimer		/* 105 */
	.llong .compat_sys_newstat
	.llong .compat_sys_newlstat
	.llong .compat_sys_newfstat
	.llong .sys32_uname
	.llong .sys_ni_syscall		/* 110 old iopl syscall */
	.llong .sys_vhangup
	.llong .sys_ni_syscall		/* old idle syscall */
	.llong .sys_ni_syscall		/* old vm86 syscall */
	.llong .compat_sys_wait4
	.llong .sys_swapoff		/* 115 */
	.llong .sys32_sysinfo
	.llong .sys32_ipc
	.llong .sys_fsync
	.llong .ppc32_sigreturn
	.llong .ppc_clone		/* 120 */
	.llong .sys32_setdomainname
	.llong .ppc64_newuname
	.llong .sys_ni_syscall		/* old modify_ldt syscall */
	.llong .sys32_adjtimex
	.llong .sys_mprotect		/* 125 */
	.llong .compat_sys_sigprocmask
	.llong .sys_ni_syscall		/* old create_module syscall */
	.llong .sys_init_module
	.llong .sys_delete_module
	.llong .sys_ni_syscall		/* 130 old get_kernel_syms syscall */
	.llong .sys_quotactl
	.llong .sys32_getpgid
	.llong .sys_fchdir
	.llong .sys_bdflush
	.llong .sys32_sysfs		/* 135 */
	.llong .ppc64_personality
	.llong .sys_ni_syscall	        /* for afs_syscall */
	.llong .sys_setfsuid
	.llong .sys_setfsgid
	.llong .sys_llseek	        /* 140 */
        .llong .sys32_getdents
	.llong .ppc32_select
	.llong .sys_flock
	.llong .sys_msync
	.llong .compat_sys_readv	/* 145 */
	.llong .compat_sys_writev
	.llong .sys32_getsid
	.llong .sys_fdatasync
	.llong .sys32_sysctl
	.llong .sys_mlock		/* 150 */
	.llong .sys_munlock
	.llong .sys_mlockall
	.llong .sys_munlockall
	.llong .sys32_sched_setparam
	.llong .sys32_sched_getparam	/* 155 */
	.llong .sys32_sched_setscheduler
	.llong .sys32_sched_getscheduler
	.llong .sys_sched_yield
	.llong .sys32_sched_get_priority_max
	.llong .sys32_sched_get_priority_min  /* 160 */
	.llong .sys32_sched_rr_get_interval
	.llong .compat_sys_nanosleep
	.llong .sys_mremap
	.llong .sys_setresuid
	.llong .sys_getresuid	        /* 165 */
	.llong .sys_ni_syscall		/* old query_module syscall */
	.llong .sys_poll
	.llong .compat_sys_nfsservctl
	.llong .sys_setresgid
	.llong .sys_getresgid	        /* 170 */
	.llong .sys32_prctl
	.llong .ppc32_rt_sigreturn
	.llong .sys32_rt_sigaction
	.llong .sys32_rt_sigprocmask
	.llong .sys32_rt_sigpending     /* 175 */
	.llong .compat_sys_rt_sigtimedwait
	.llong .sys32_rt_sigqueueinfo
	.llong .ppc32_rt_sigsuspend
	.llong .sys32_pread64
	.llong .sys32_pwrite64	        /* 180 */
	.llong .sys_chown
	.llong .sys_getcwd
	.llong .sys_capget
	.llong .sys_capset
	.llong .sys32_sigaltstack	/* 185 */
	.llong .sys32_sendfile
	.llong .sys_ni_syscall		/* reserved for streams1 */
	.llong .sys_ni_syscall		/* reserved for streams2 */
	.llong .ppc_vfork
	.llong .compat_sys_getrlimit	        /* 190 */
	.llong .sys32_readahead
	.llong .sys32_mmap2
	.llong .sys32_truncate64
	.llong .sys32_ftruncate64
	.llong .sys_stat64      	/* 195 */
	.llong .sys_lstat64
	.llong .sys_fstat64
	.llong .sys32_pciconfig_read
	.llong .sys32_pciconfig_write
	.llong .sys32_pciconfig_iobase	/* 200 - pciconfig_iobase */
	.llong .sys_ni_syscall		/* reserved for MacOnLinux */
	.llong .sys_getdents64
	.llong .sys_pivot_root
	.llong .compat_sys_fcntl64
	.llong .sys_madvise		/* 205 */
	.llong .sys_mincore
	.llong .sys_gettid
	.llong .sys_tkill
	.llong .sys_setxattr
	.llong .sys_lsetxattr		/* 210 */
	.llong .sys_fsetxattr
	.llong .sys_getxattr
	.llong .sys_lgetxattr
	.llong .sys_fgetxattr
	.llong .sys_listxattr		/* 215 */
	.llong .sys_llistxattr
	.llong .sys_flistxattr
	.llong .sys_removexattr
	.llong .sys_lremovexattr
	.llong .sys_fremovexattr	/* 220 */
	.llong .compat_sys_futex
	.llong .compat_sys_sched_setaffinity
	.llong .compat_sys_sched_getaffinity
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* 225 - reserved for tux */
	.llong .sys32_sendfile64
	.llong .compat_sys_io_setup
	.llong .sys_io_destroy
	.llong .compat_sys_io_getevents
	.llong .compat_sys_io_submit
	.llong .sys_io_cancel
	.llong .sys_set_tid_address
	.llong .ppc32_fadvise64
	.llong .sys_exit_group
	.llong .ppc32_lookup_dcookie	/* 235 */
	.llong .sys_epoll_create
	.llong .sys_epoll_ctl
	.llong .sys_epoll_wait
	.llong .sys_remap_file_pages
	.llong .ppc32_timer_create	/* 240 */
	.llong .compat_sys_timer_settime
	.llong .compat_sys_timer_gettime
	.llong .sys_timer_getoverrun
	.llong .sys_timer_delete
	.llong .compat_sys_clock_settime	/* 245 */
	.llong .compat_sys_clock_gettime
	.llong .compat_sys_clock_getres
	.llong .compat_sys_clock_nanosleep
	.llong .ppc32_swapcontext
	.llong .sys32_tgkill		/* 250 */
	.llong .sys32_utimes
	.llong .compat_sys_statfs64
	.llong .compat_sys_fstatfs64
	.llong .ppc32_fadvise64_64	/* 32bit only fadvise64_64 */
	.llong .ppc_rtas		/* 255 */
	.llong .sys_ni_syscall		/* 256 reserved for sys_debug_setcontext */
	.llong .sys_ni_syscall		/* 257 reserved for vserver */
	.llong .sys_ni_syscall		/* 258 reserved for new sys_remap_file_pages */
	.llong .compat_sys_mbind
	.llong .compat_sys_get_mempolicy	/* 260 */
	.llong .compat_sys_set_mempolicy
	.llong .compat_sys_mq_open
	.llong .sys_mq_unlink
	.llong .compat_sys_mq_timedsend
	.llong .compat_sys_mq_timedreceive /* 265 */
	.llong .compat_sys_mq_notify
	.llong .compat_sys_mq_getsetattr
	.llong .compat_sys_kexec_load
	.llong .sys32_add_key
	.llong .sys32_request_key	/* 270 */
	.llong .compat_sys_keyctl
	.llong .compat_sys_waitid
	.llong .sys32_ioprio_set
	.llong .sys32_ioprio_get
	.llong .sys_inotify_init	/* 275 */
	.llong .sys_inotify_add_watch
	.llong .sys_inotify_rm_watch

	.balign 8
_GLOBAL(sys_call_table)
	.llong .sys_restart_syscall	/* 0 */
	.llong .sys_exit
	.llong .ppc_fork
	.llong .sys_read
	.llong .sys_write
	.llong .sys_open		/* 5 */
	.llong .sys_close
	.llong .sys_waitpid
	.llong .sys_creat
	.llong .sys_link
	.llong .sys_unlink		/* 10 */
	.llong .sys_execve
	.llong .sys_chdir
	.llong .sys64_time
	.llong .sys_mknod
	.llong .sys_chmod		/* 15 */
	.llong .sys_lchown
	.llong .sys_ni_syscall		/* old break syscall */
	.llong .sys_ni_syscall		/* old stat syscall */
	.llong .sys_lseek
	.llong .sys_getpid		/* 20 */
	.llong .sys_mount
	.llong .sys_ni_syscall		/* old umount syscall */
	.llong .sys_setuid
	.llong .sys_getuid
	.llong .sys_stime		/* 25 */
	.llong .sys_ptrace
	.llong .sys_alarm
	.llong .sys_ni_syscall		/* old fstat syscall */
	.llong .sys_pause
	.llong .sys_utime		/* 30 */
	.llong .sys_ni_syscall		/* old stty syscall */
	.llong .sys_ni_syscall		/* old gtty syscall */
	.llong .sys_access
	.llong .sys_nice
	.llong .sys_ni_syscall		/* 35 - old ftime syscall */
	.llong .sys_sync
	.llong .sys_kill
	.llong .sys_rename
	.llong .sys_mkdir
	.llong .sys_rmdir		/* 40 */
	.llong .sys_dup
	.llong .sys_pipe
	.llong .sys_times
	.llong .sys_ni_syscall		/* old prof syscall */
	.llong .sys_brk			/* 45 */
	.llong .sys_setgid
	.llong .sys_getgid
	.llong .sys_signal
	.llong .sys_geteuid
	.llong .sys_getegid		/* 50 */
	.llong .sys_acct
	.llong .sys_umount
	.llong .sys_ni_syscall		/* old lock syscall */
	.llong .sys_ioctl
	.llong .sys_fcntl		/* 55 */
	.llong .sys_ni_syscall		/* old mpx syscall */
	.llong .sys_setpgid
	.llong .sys_ni_syscall		/* old ulimit syscall */
	.llong .sys_ni_syscall		/* old uname syscall */
	.llong .sys_umask		/* 60 */
	.llong .sys_chroot
	.llong .sys_ustat
	.llong .sys_dup2
	.llong .sys_getppid
	.llong .sys_getpgrp		/* 65 */
	.llong .sys_setsid
	.llong .sys_ni_syscall
	.llong .sys_sgetmask
	.llong .sys_ssetmask
	.llong .sys_setreuid		/* 70 */
	.llong .sys_setregid
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall
	.llong .sys_sethostname
	.llong .sys_setrlimit		/* 75 */
	.llong .sys_ni_syscall		/* old getrlimit syscall */
	.llong .sys_getrusage
	.llong .sys_gettimeofday
	.llong .sys_settimeofday
	.llong .sys_getgroups		/* 80 */
	.llong .sys_setgroups
	.llong .sys_ni_syscall		/* old select syscall */
	.llong .sys_symlink
	.llong .sys_ni_syscall		/* old lstat syscall */
	.llong .sys_readlink		/* 85 */
	.llong .sys_uselib
	.llong .sys_swapon
	.llong .sys_reboot
	.llong .sys_ni_syscall		/* old readdir syscall */
	.llong .sys_mmap		/* 90 */
	.llong .sys_munmap
	.llong .sys_truncate
	.llong .sys_ftruncate
	.llong .sys_fchmod
	.llong .sys_fchown		/* 95 */
	.llong .sys_getpriority
	.llong .sys_setpriority
	.llong .sys_ni_syscall		/* old profil syscall holder */
	.llong .sys_statfs
	.llong .sys_fstatfs		/* 100 */
	.llong .sys_ni_syscall		/* old ioperm syscall */
	.llong .sys_socketcall
	.llong .sys_syslog
	.llong .sys_setitimer
	.llong .sys_getitimer		/* 105 */
	.llong .sys_newstat
	.llong .sys_newlstat
	.llong .sys_newfstat
	.llong .sys_ni_syscall		/* old uname syscall */
	.llong .sys_ni_syscall		/* 110 old iopl syscall */
	.llong .sys_vhangup
	.llong .sys_ni_syscall		/* old idle syscall */
	.llong .sys_ni_syscall		/* old vm86 syscall */
	.llong .sys_wait4
	.llong .sys_swapoff		/* 115 */
	.llong .sys_sysinfo
	.llong .sys_ipc
	.llong .sys_fsync
	.llong .sys_ni_syscall
	.llong .ppc_clone		/* 120 */
	.llong .sys_setdomainname
	.llong .ppc64_newuname
	.llong .sys_ni_syscall		/* old modify_ldt syscall */
	.llong .sys_adjtimex
	.llong .sys_mprotect		/* 125 */
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* old create_module syscall */
	.llong .sys_init_module
	.llong .sys_delete_module
	.llong .sys_ni_syscall		/* 130 old get_kernel_syms syscall */
	.llong .sys_quotactl
	.llong .sys_getpgid
	.llong .sys_fchdir
	.llong .sys_bdflush
	.llong .sys_sysfs		/* 135 */
	.llong .ppc64_personality
	.llong .sys_ni_syscall	        /* for afs_syscall */
	.llong .sys_setfsuid
	.llong .sys_setfsgid
	.llong .sys_llseek	        /* 140 */
        .llong .sys_getdents
	.llong .sys_select
	.llong .sys_flock
	.llong .sys_msync
	.llong .sys_readv		/* 145 */
	.llong .sys_writev
	.llong .sys_getsid
	.llong .sys_fdatasync
	.llong .sys_sysctl
	.llong .sys_mlock		/* 150 */
	.llong .sys_munlock
	.llong .sys_mlockall
	.llong .sys_munlockall
	.llong .sys_sched_setparam
	.llong .sys_sched_getparam	/* 155 */
	.llong .sys_sched_setscheduler
	.llong .sys_sched_getscheduler
	.llong .sys_sched_yield
	.llong .sys_sched_get_priority_max
	.llong .sys_sched_get_priority_min  /* 160 */
	.llong .sys_sched_rr_get_interval
	.llong .sys_nanosleep
	.llong .sys_mremap
	.llong .sys_setresuid
	.llong .sys_getresuid	        /* 165 */
	.llong .sys_ni_syscall		/* old query_module syscall */
	.llong .sys_poll
	.llong .sys_nfsservctl
	.llong .sys_setresgid
	.llong .sys_getresgid	        /* 170 */
	.llong .sys_prctl
	.llong .ppc64_rt_sigreturn
	.llong .sys_rt_sigaction
	.llong .sys_rt_sigprocmask	
	.llong .sys_rt_sigpending	/* 175 */
	.llong .sys_rt_sigtimedwait
	.llong .sys_rt_sigqueueinfo
	.llong .ppc64_rt_sigsuspend
	.llong .sys_pread64
	.llong .sys_pwrite64	        /* 180 */
	.llong .sys_chown
	.llong .sys_getcwd
	.llong .sys_capget
	.llong .sys_capset
	.llong .sys_sigaltstack	        /* 185 */
	.llong .sys_sendfile64
	.llong .sys_ni_syscall		/* reserved for streams1 */
	.llong .sys_ni_syscall		/* reserved for streams2 */
	.llong .ppc_vfork
	.llong .sys_getrlimit	        /* 190 */
	.llong .sys_readahead
	.llong .sys_ni_syscall		/* 32bit only mmap2 */
	.llong .sys_ni_syscall		/* 32bit only truncate64 */
	.llong .sys_ni_syscall		/* 32bit only ftruncate64 */
	.llong .sys_ni_syscall		/* 195 - 32bit only stat64 */
	.llong .sys_ni_syscall		/* 32bit only lstat64 */
	.llong .sys_ni_syscall		/* 32bit only fstat64 */
	.llong .sys_ni_syscall		/* 32bit only pciconfig_read */
	.llong .sys_ni_syscall		/* 32bit only pciconfig_write */
	.llong .sys_ni_syscall		/* 32bit only pciconfig_iobase */
	.llong .sys_ni_syscall		/* reserved for MacOnLinux */
	.llong .sys_getdents64
	.llong .sys_pivot_root
	.llong .sys_ni_syscall		/* 32bit only fcntl64 */
	.llong .sys_madvise		/* 205 */
	.llong .sys_mincore
	.llong .sys_gettid
	.llong .sys_tkill
	.llong .sys_setxattr
	.llong .sys_lsetxattr		/* 210 */
	.llong .sys_fsetxattr
	.llong .sys_getxattr
	.llong .sys_lgetxattr
	.llong .sys_fgetxattr
	.llong .sys_listxattr		/* 215 */
	.llong .sys_llistxattr
	.llong .sys_flistxattr
	.llong .sys_removexattr
	.llong .sys_lremovexattr
	.llong .sys_fremovexattr	/* 220 */
	.llong .sys_futex
	.llong .sys_sched_setaffinity
	.llong .sys_sched_getaffinity
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* 225 - reserved for tux */
	.llong .sys_ni_syscall		/* 32bit only sendfile64 */
	.llong .sys_io_setup
	.llong .sys_io_destroy
	.llong .sys_io_getevents
	.llong .sys_io_submit		/* 230 */
	.llong .sys_io_cancel
	.llong .sys_set_tid_address
	.llong .sys_fadvise64
	.llong .sys_exit_group
	.llong .sys_lookup_dcookie	/* 235 */
	.llong .sys_epoll_create
	.llong .sys_epoll_ctl
	.llong .sys_epoll_wait
	.llong .sys_remap_file_pages
	.llong .sys_timer_create	/* 240 */
	.llong .sys_timer_settime
	.llong .sys_timer_gettime
	.llong .sys_timer_getoverrun
	.llong .sys_timer_delete
	.llong .sys_clock_settime	/* 245 */
	.llong .sys_clock_gettime
	.llong .sys_clock_getres
	.llong .sys_clock_nanosleep
	.llong .ppc64_swapcontext
	.llong .sys_tgkill		/* 250 */
	.llong .sys_utimes
	.llong .sys_statfs64
	.llong .sys_fstatfs64
	.llong .sys_ni_syscall		/* 32bit only fadvise64_64 */
	.llong .ppc_rtas		/* 255 */
	.llong .sys_ni_syscall		/* 256 reserved for sys_debug_setcontext */
	.llong .sys_ni_syscall		/* 257 reserved for vserver */
	.llong .sys_ni_syscall		/* 258 reserved for new sys_remap_file_pages */
	.llong .sys_mbind
	.llong .sys_get_mempolicy	/* 260 */
	.llong .sys_set_mempolicy
	.llong .sys_mq_open
	.llong .sys_mq_unlink
	.llong .sys_mq_timedsend
	.llong .sys_mq_timedreceive	/* 265 */
	.llong .sys_mq_notify
	.llong .sys_mq_getsetattr
	.llong .sys_kexec_load
	.llong .sys_add_key
	.llong .sys_request_key		/* 270 */
	.llong .sys_keyctl
	.llong .sys_waitid
	.llong .sys_ioprio_set
	.llong .sys_ioprio_get
	.llong .sys_inotify_init	/* 275 */
	.llong .sys_inotify_add_watch
	.llong .sys_inotify_rm_watch
