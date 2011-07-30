/*
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

#ifdef CONFIG_IRQSTACKS
_GLOBAL(call_do_softirq)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r3)
	mr	r1,r3
	bl	.__do_softirq
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr

_GLOBAL(call_handle_irq)
	ld	r8,0(r6)
	mflr	r0
	std	r0,16(r1)
	mtctr	r8
	stdu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r5)
	mr	r1,r5
	bctrl
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr
#endif /* CONFIG_IRQSTACKS */

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

#ifdef CONFIG_PPC_PASEMI

/* No support in all binutils for these yet, so use defines */
#define LBZCIX(RT,RA,RB)  .long (0x7c0006aa|(RT<<21)|(RA<<16)|(RB << 11))
#define STBCIX(RS,RA,RB)  .long (0x7c0007aa|(RS<<21)|(RA<<16)|(RB << 11))


_GLOBAL(real_205_readb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsrd	r0
	sync
	isync
	LBZCIX(r3,0,r3)
	isync
	mtmsrd	r7
	sync
	isync
	blr

_GLOBAL(real_205_writeb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsrd	r0
	sync
	isync
	STBCIX(r3,0,r4)
	isync
	mtmsrd	r7
	sync
	isync
	blr

#endif /* CONFIG_PPC_PASEMI */


#ifdef CONFIG_CPU_FREQ_PMAC64
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
#endif /* CONFIG_CPU_FREQ_PMAC64 */


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
	bns+	1f		/* did system call indicate error? */
	neg	r3,r3		/* if so, make return code negative */
1:	cmpdi	0,r3,0		/* parent or child? */
	bne	2f		/* return if parent */
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
2:	addi	r1,r1,STACK_FRAME_OVERHEAD
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
	stdu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r3)
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

	stdu	r1,-STACK_FRAME_OVERHEAD-64(r1)

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

	/* copy  0x100 bytes starting at start to 0 */
	li	r3,0
	mr	r4,r30		/* start, aka phys mem offset */
	li	r5,0x100
	li	r6,0
	bl	.copy_and_flush	/* (dest, src, copy limit, start offset) */
1:	/* assume normal blr return */

	/* release other cpus to the new kernel secondary start at 0x60 */
	mflr	r5
	li	r6,1
	stw	r6,kexec_flag-1b(5)

	/* clear out hardware hash page table and tlb */
	ld	r5,0(r27)		/* deref function descriptor */
	mtctr	r5
	bctrl				/* ppc_md.hpte_clear_all(void); */

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
	mr	r3,r25	# my phys cpu
	mr	r4,r30	# start, aka phys mem offset
	mtlr	4
	li	r5,0
	blr	/* image->start(physid, image->start, 0); */
#endif /* CONFIG_KEXEC */
