/*
 * This is the common part of the loader relocation and initialization
 * process.  All of the board/processor specific initialization is
 * done before we get here.
 *
 * Author: Tom Rini
 *	   trini@mvista.com
 * Derived from arch/ppc/boot/prep/head.S (Cort Dougan, many others).
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>

#define GETSYM(reg, sym)	\
	lis	reg, sym@h; ori	reg, reg, sym@l

	.text
	/* We get called from the early initialization code.
	 * Register 3 has the address where we were loaded,
	 * Register 4 contains any residual data passed from the
	 * boot rom.
	 */
	.globl	relocate
relocate:
	/* Save r3, r4 for later.
	 * The r8/r11 are legacy registers so I don't have to
	 * rewrite the code below :-).
	 */
	mr	r8, r3
	mr	r11, r4

	/* compute the size of the whole image in words. */
	GETSYM(r4,start)
	GETSYM(r5,end)

	addi	r5,r5,3		/* round up */
	sub	r5,r5,r4	/* end - start */
	srwi	r5,r5,2
	mr	r7,r5		/* Save for later use. */

	/*
	 * Check if we need to relocate ourselves to the link addr or were
	 * we loaded there to begin with.
	 */
	cmpw	cr0,r3,r4
	beq	start_ldr	/* If 0, we don't need to relocate */

	/* Move this code somewhere safe.  This is max(load + size, end)
	 * r8 == load address
	 */
	GETSYM(r4, start)
	GETSYM(r5, end)

	sub	r6,r5,r4
	add	r6,r8,r6	/* r6 == phys(load + size) */

	cmpw	r5,r6
	bgt	1f
	b	2f
1:
	mr	r6, r5
2:
	/* dest is in r6 */
	/* Ensure alignment --- this code is precautionary */
	addi	r6,r6,4
	li	r5,0x0003
	andc	r6,r6,r5

	/* Find physical address and size of do_relocate */
	GETSYM(r5, __relocate_start)
	GETSYM(r4, __relocate_end)
	GETSYM(r3, start)

	/* Size to copy */
	sub	r4,r4,r5
	srwi	r4,r4,2

	/* Src addr to copy (= __relocate_start - start + where_loaded) */
	sub	r3,r5,r3
	add	r5,r8,r3

	/* Save dest */
	mr	r3, r6

	/* Do the copy */
	mtctr	r4
3:	lwz	r4,0(r5)
	stw	r4,0(r3)
	addi	r3,r3,4
	addi	r5,r5,4
	bdnz	3b

	GETSYM(r4, __relocate_start)
	GETSYM(r5, do_relocate)

	sub	r4,r5,r4	/* Get entry point for do_relocate in */
	add	r6,r6,r4	/* relocated section */

	/* This will return to the relocated do_relocate */
	mtlr	r6
	b	flush_instruction_cache

	.section ".relocate_code","xa"
	
do_relocate:
	/* We have 2 cases --- start < load, or start > load
	 * This determines whether we copy from the end, or the start.
	 * Its easier to have 2 loops than to have paramaterised
	 * loops.  Sigh.
	 */
	li	r6,0		/* Clear checksum */
	mtctr	r7		/* Setup for a loop */
	
	GETSYM(r4, start)
	mr	r3,r8		/* Get the load addr */

	cmpw	cr0,r4,r3	/* If we need to copy from the end, do so */
	bgt	do_relocate_from_end

do_relocate_from_start:
1:	lwz	r5,0(r3)	/* Load and decrement */
	stw	r5,0(r4)	/* Store and decrement */
	addi	r3,r3,4
	addi	r4,r4,4
	xor	r6,r6,r5	/* Update checksum */
	bdnz	1b		/* Are we done? */
	b	do_relocate_out	/* Finished */

do_relocate_from_end:
	GETSYM(r3, end)
	slwi	r4,r7,2
	add	r4,r8,r4	/* Get the physical end */
1:	lwzu	r5,-4(r4)
	stwu	r5, -4(r3)
	xor	r6,r6,r5
	bdnz	1b

do_relocate_out:
	GETSYM(r3,start_ldr)
	mtlr	r3		/* Easiest way to do an absolute jump */
/* Some boards don't boot up with the I-cache enabled.  Do that
 * now because the decompress runs much faster that way.
 * As a side effect, we have to ensure the data cache is not enabled
 * so we can access the serial I/O without trouble.
 */
	b	flush_instruction_cache

	.previous

start_ldr:
/* Clear all of BSS and set up stack for C calls */
	lis	r3,edata@h
	ori	r3,r3,edata@l
	lis	r4,end@h
	ori	r4,r4,end@l
	subi	r3,r3,4
	subi	r4,r4,4
	li	r0,0
50:	stwu	r0,4(r3)
	cmpw	cr0,r3,r4
	bne	50b
90:	mr	r9,r1		/* Save old stack pointer (in case it matters) */
	lis	r1,.stack@h
	ori	r1,r1,.stack@l
	addi	r1,r1,4096*2
	subi	r1,r1,256
	li	r2,0x000F	/* Mask pointer to 16-byte boundary */
	andc	r1,r1,r2

	/*
	 * Exec kernel loader
	 */
	mr	r3,r8		/* Load point */
	mr	r4,r7		/* Program length */
	mr	r5,r6		/* Checksum */
	mr	r6,r11		/* Residual data */
	mr	r7,r25		/* Validated OFW interface */
	bl	load_kernel

	/*
	 * Make sure the kernel knows we don't have things set in
	 * registers.  -- Tom
	 */
	li	r4,0
	li	r5,0
	li	r6,0

	/*
	 * Start at the begining.
	 */
#ifdef CONFIG_PPC_MULTIPLATFORM
	li	r9,0xc
	mtlr	r9
	/* tell kernel we're prep, by putting 0xdeadc0de at KERNELLOAD,
	 * and tell the kernel to start on the 4th instruction since we
	 * overwrite the first 3 sometimes (which are 'nop').
	 */
	lis	r10,0xdeadc0de@h
	ori	r10,r10,0xdeadc0de@l
	li	r9,0
	stw	r10,0(r9)
#else
	li	r9,0
	mtlr	r9
#endif
	blr

	.comm	.stack,4096*2,4
