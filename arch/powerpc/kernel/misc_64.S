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
#include <asm/kexec.h>
#include <asm/ptrace.h>
#include <asm/mmu.h>
#include <asm/export.h>
#include <asm/feature-fixups.h>

	.text

_GLOBAL(call_do_softirq)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r3)
	mr	r1,r3
	bl	__do_softirq
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr

_GLOBAL(call_do_irq)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r4)
	mr	r1,r4
	bl	__do_irq
	ld	r1,0(r1)
	ld	r0,16(r1)
	mtlr	r0
	blr

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

_GLOBAL_TOC(flush_icache_range)
BEGIN_FTR_SECTION
	PURGE_PREFETCHED_INS
	blr
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)
/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 * and in some cases i-cache and d-cache line sizes differ from
 * each other.
 */
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1BLOCKSIZE(r10)/* Get cache block size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGBLOCKSIZE(r10)	/* Get log-2 of cache block size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	1b
	sync

/* Now invalidate the instruction cache */
	
	lwz	r7,ICACHEL1BLOCKSIZE(r10)	/* Get Icache block size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lwz	r9,ICACHEL1LOGBLOCKSIZE(r10)	/* Get log-2 of Icache block size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
2:	icbi	0,r6
	add	r6,r6,r7
	bdnz	2b
	isync
	blr
_ASM_NOKPROBE_SYMBOL(flush_icache_range)
EXPORT_SYMBOL(flush_icache_range)

/*
 * Like above, but only do the D-cache.
 *
 * flush_dcache_range(unsigned long start, unsigned long stop)
 *
 *    flush all bytes from start to stop-1 inclusive
 */

_GLOBAL_TOC(flush_dcache_range)
 	ld	r10,PPC64_CACHES@toc(r2)
	lwz	r7,DCACHEL1BLOCKSIZE(r10)	/* Get dcache block size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGBLOCKSIZE(r10)/* Get log-2 of dcache block size */
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
EXPORT_SYMBOL(flush_dcache_range)

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

BEGIN_FTR_SECTION
	PURGE_PREFETCHED_INS
	blr
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)

/* Flush the dcache */
 	ld	r7,PPC64_CACHES@toc(r2)
	clrrdi	r3,r3,PAGE_SHIFT           	    /* Page align */
	lwz	r4,DCACHEL1BLOCKSPERPAGE(r7)	/* Get # dcache blocks per page */
	lwz	r5,DCACHEL1BLOCKSIZE(r7)	/* Get dcache block size */
	mr	r6,r3
	mtctr	r4
0:	dcbst	0,r6
	add	r6,r6,r5
	bdnz	0b
	sync

/* Now invalidate the icache */	

	lwz	r4,ICACHEL1BLOCKSPERPAGE(r7)	/* Get # icache blocks per page */
	lwz	r5,ICACHEL1BLOCKSIZE(r7)	/* Get icache block size */
	mtctr	r4
1:	icbi	0,r3
	add	r3,r3,r5
	bdnz	1b
	isync
	blr

_GLOBAL(__bswapdi2)
EXPORT_SYMBOL(__bswapdi2)
	srdi	r8,r3,32
	rlwinm	r7,r3,8,0xffffffff
	rlwimi	r7,r3,24,0,7
	rlwinm	r9,r8,8,0xffffffff
	rlwimi	r7,r3,24,16,23
	rlwimi	r9,r8,24,0,7
	rlwimi	r9,r8,24,16,23
	sldi	r7,r7,32
	or	r3,r7,r9
	blr


#ifdef CONFIG_PPC_EARLY_DEBUG_BOOTX
_GLOBAL(rmci_on)
	sync
	isync
	li	r3,0x100
	rldicl	r3,r3,32,0
	mfspr	r5,SPRN_HID4
	or	r5,r5,r3
	sync
	mtspr	SPRN_HID4,r5
	isync
	slbia
	isync
	sync
	blr

_GLOBAL(rmci_off)
	sync
	isync
	li	r3,0x100
	rldicl	r3,r3,32,0
	mfspr	r5,SPRN_HID4
	andc	r5,r5,r3
	sync
	mtspr	SPRN_HID4,r5
	isync
	slbia
	isync
	sync
	blr
#endif /* CONFIG_PPC_EARLY_DEBUG_BOOTX */

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

_GLOBAL(real_205_readb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsrd	r0
	sync
	isync
	LBZCIX(R3,R0,R3)
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
	STBCIX(R3,R0,R4)
	isync
	mtmsrd	r7
	sync
	isync
	blr

#endif /* CONFIG_PPC_PASEMI */


#if defined(CONFIG_CPU_FREQ_PMAC64) || defined(CONFIG_CPU_FREQ_MAPLE)
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
#endif /* CONFIG_CPU_FREQ_PMAC64 || CONFIG_CPU_FREQ_MAPLE */

/* kexec_wait(phys_cpu)
 *
 * wait for the flag to change, indicating this kernel is going away but
 * the slave code for the next one is at addresses 0 to 100.
 *
 * This is used by all slaves, even those that did not find a matching
 * paca in the secondary startup code.
 *
 * Physical (hardware) cpu id should be in r3.
 */
_GLOBAL(kexec_wait)
	bl	1f
1:	mflr	r5
	addi	r5,r5,kexec_flag-1b

99:	HMT_LOW
#ifdef CONFIG_KEXEC_CORE	/* use no memory without kexec */
	lwz	r4,0(r5)
	cmpwi	0,r4,0
	beq	99b
#ifdef CONFIG_PPC_BOOK3S_64
	li	r10,0x60
	mfmsr	r11
	clrrdi	r11,r11,1	/* Clear MSR_LE */
	mtsrr0	r10
	mtsrr1	r11
	rfid
#else
	/* Create TLB entry in book3e_secondary_core_init */
	li	r4,0
	ba	0x60
#endif
#endif

/* this can be in text because we won't change it until we are
 * running in real anyways
 */
kexec_flag:
	.long	0


#ifdef CONFIG_KEXEC_CORE
#ifdef CONFIG_PPC_BOOK3E
/*
 * BOOK3E has no real MMU mode, so we have to setup the initial TLB
 * for a core to identity map v:0 to p:0.  This current implementation
 * assumes that 1G is enough for kexec.
 */
kexec_create_tlb:
	/*
	 * Invalidate all non-IPROT TLB entries to avoid any TLB conflict.
	 * IPROT TLB entries should be >= PAGE_OFFSET and thus not conflict.
	 */
	PPC_TLBILX_ALL(0,R0)
	sync
	isync

	mfspr	r10,SPRN_TLB1CFG
	andi.	r10,r10,TLBnCFG_N_ENTRY	/* Extract # entries */
	subi	r10,r10,1	/* Last entry: no conflict with kernel text */
	lis	r9,MAS0_TLBSEL(1)@h
	rlwimi	r9,r10,16,4,15		/* Setup MAS0 = TLBSEL | ESEL(r9) */

/* Set up a temp identity mapping v:0 to p:0 and return to it. */
#if defined(CONFIG_SMP) || defined(CONFIG_PPC_E500MC)
#define M_IF_NEEDED	MAS2_M
#else
#define M_IF_NEEDED	0
#endif
	mtspr	SPRN_MAS0,r9

	lis	r9,(MAS1_VALID|MAS1_IPROT)@h
	ori	r9,r9,(MAS1_TSIZE(BOOK3E_PAGESZ_1GB))@l
	mtspr	SPRN_MAS1,r9

	LOAD_REG_IMMEDIATE(r9, 0x0 | M_IF_NEEDED)
	mtspr	SPRN_MAS2,r9

	LOAD_REG_IMMEDIATE(r9, 0x0 | MAS3_SR | MAS3_SW | MAS3_SX)
	mtspr	SPRN_MAS3,r9
	li	r9,0
	mtspr	SPRN_MAS7,r9

	tlbwe
	isync
	blr
#endif

/* kexec_smp_wait(void)
 *
 * call with interrupts off
 * note: this is a terminal routine, it does not save lr
 *
 * get phys id from paca
 * switch to real mode
 * mark the paca as no longer used
 * join other cpus in kexec_wait(phys_id)
 */
_GLOBAL(kexec_smp_wait)
	lhz	r3,PACAHWCPUID(r13)
	bl	real_mode

	li	r4,KEXEC_STATE_REAL_MODE
	stb	r4,PACAKEXECSTATE(r13)
	SYNC

	b	kexec_wait

/*
 * switch to real mode (turn mmu off)
 * we use the early kernel trick that the hardware ignores bits
 * 0 and 1 (big endian) of the effective address in real mode
 *
 * don't overwrite r3 here, it is live for kexec_wait above.
 */
real_mode:	/* assume normal blr return */
#ifdef CONFIG_PPC_BOOK3E
	/* Create an identity mapping. */
	b	kexec_create_tlb
#else
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
#endif

/*
 * kexec_sequence(newstack, start, image, control, clear_all(),
	          copy_with_mmu_off)
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

BEGIN_FTR_SECTION
	/*
	 * This is the best time to turn AMR/IAMR off.
	 * key 0 is used in radix for supervisor<->user
	 * protection, but on hash key 0 is reserved
	 * ideally we want to enter with a clean state.
	 * NOTE, we rely on r0 being 0 from above.
	 */
	mtspr	SPRN_IAMR,r0
BEGIN_FTR_SECTION_NESTED(42)
	mtspr	SPRN_AMOR,r0
END_FTR_SECTION_NESTED_IFSET(CPU_FTR_HVMODE, 42)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)

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
	mr	r26,r8			/* copy_with_mmu_off */
	lhz	r25,PACAHWCPUID(r13)	/* get our phys cpu from paca */

	/* disable interrupts, we are overwriting kernel data next */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	0
#else
	mfmsr	r3
	rlwinm	r3,r3,0,17,15
	mtmsrd	r3,1
#endif

	/* We need to turn the MMU off unless we are in hash mode
	 * under a hypervisor
	 */
	cmpdi	r26,0
	beq	1f
	bl	real_mode
1:
	/* copy dest pages, flush whole dest image */
	mr	r3,r29
	bl	kexec_copy_flush	/* (image) */

	/* turn off mmu now if not done earlier */
	cmpdi	r26,0
	bne	1f
	bl	real_mode

	/* copy  0x100 bytes starting at start to 0 */
1:	li	r3,0
	mr	r4,r30		/* start, aka phys mem offset */
	li	r5,0x100
	li	r6,0
	bl	copy_and_flush	/* (dest, src, copy limit, start offset) */
1:	/* assume normal blr return */

	/* release other cpus to the new kernel secondary start at 0x60 */
	mflr	r5
	li	r6,1
	stw	r6,kexec_flag-1b(5)

	cmpdi	r27,0
	beq	1f

	/* clear out hardware hash page table and tlb */
#ifdef PPC64_ELF_ABI_v1
	ld	r12,0(r27)		/* deref function descriptor */
#else
	mr	r12,r27
#endif
	mtctr	r12
	bctrl				/* mmu_hash_ops.hpte_clear_all(void); */

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
1:	mr	r3,r25	# my phys cpu
	mr	r4,r30	# start, aka phys mem offset
	mtlr	4
	li	r5,0
	blr	/* image->start(physid, image->start, 0); */
#endif /* CONFIG_KEXEC_CORE */
