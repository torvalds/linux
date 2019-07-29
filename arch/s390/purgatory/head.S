/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Purgatory setup code
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Philipp Rudo <prudo@linux.vnet.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/sigp.h>
#include <asm/ptrace.h>

/* The purgatory is the code running between two kernels. It's main purpose
 * is to verify that the next kernel was not corrupted after load and to
 * start it.
 *
 * If the next kernel is a crash kernel there are some peculiarities to
 * consider:
 *
 * First the purgatory is called twice. Once only to verify the
 * sha digest. So if the crash kernel got corrupted the old kernel can try
 * to trigger a stand-alone dumper. And once to actually load the crash kernel.
 *
 * Second the purgatory also has to swap the crash memory region with its
 * destination at address 0. As the purgatory is part of crash memory this
 * requires some finesse. The tactic here is that the purgatory first copies
 * itself to the end of the destination and then swaps the rest of the
 * memory running from there.
 */

#define bufsz purgatory_end-stack

.macro MEMCPY dst,src,len
	lgr	%r0,\dst
	lgr	%r1,\len
	lgr	%r2,\src
	lgr	%r3,\len

20:	mvcle	%r0,%r2,0
	jo	20b
.endm

.macro MEMSWAP dst,src,buf,len
10:	cghi	\len,bufsz
	jh	11f
	lgr	%r4,\len
	j	12f
11:	lghi	%r4,bufsz

12:	MEMCPY	\buf,\dst,%r4
	MEMCPY	\dst,\src,%r4
	MEMCPY	\src,\buf,%r4

	agr	\dst,%r4
	agr	\src,%r4
	sgr	\len,%r4

	cghi	\len,0
	jh	10b
.endm

.macro START_NEXT_KERNEL base
	lg	%r4,kernel_entry-\base(%r13)
	lg	%r5,load_psw_mask-\base(%r13)
	ogr	%r4,%r5
	stg	%r4,0(%r0)

	xgr	%r0,%r0
	diag	%r0,%r0,0x308
.endm

.text
.align PAGE_SIZE
ENTRY(purgatory_start)
	/* The purgatory might be called after a diag308 so better set
	 * architecture and addressing mode.
	 */
	lhi	%r1,1
	sigp	%r1,%r0,SIGP_SET_ARCHITECTURE
	sam64

	larl	%r5,gprregs
	stmg	%r6,%r15,0(%r5)

	basr	%r13,0
.base_crash:

	/* Setup stack */
	larl	%r15,purgatory_end-STACK_FRAME_OVERHEAD

	/* If the next kernel is KEXEC_TYPE_CRASH the purgatory is called
	 * directly with a flag passed in %r2 whether the purgatory shall do
	 * checksum verification only (%r2 = 0 -> verification only).
	 *
	 * Check now and preserve over C function call by storing in
	 * %r10 whith
	 *	1 -> checksum verification only
	 *	0 -> load new kernel
	 */
	lghi	%r10,0
	lg	%r11,kernel_type-.base_crash(%r13)
	cghi	%r11,1		/* KEXEC_TYPE_CRASH */
	jne	.do_checksum_verification
	cghi	%r2,0		/* checksum verification only */
	jne	.do_checksum_verification
	lghi	%r10,1

.do_checksum_verification:
	brasl	%r14,verify_sha256_digest

	cghi	%r10,1		/* checksum verification only */
	je	.return_old_kernel
	cghi	%r2,0		/* checksum match */
	jne	.disabled_wait

	/* If the next kernel is a crash kernel the purgatory has to swap
	 * the mem regions first.
	 */
	cghi	%r11,1 /* KEXEC_TYPE_CRASH */
	je	.start_crash_kernel

	/* start normal kernel */
	START_NEXT_KERNEL .base_crash

.return_old_kernel:
	lmg	%r6,%r15,gprregs-.base_crash(%r13)
	br	%r14

.disabled_wait:
	lpswe	disabled_wait_psw-.base_crash(%r13)

.start_crash_kernel:
	/* Location of purgatory_start in crash memory */
	lgr	%r8,%r13
	aghi	%r8,-(.base_crash-purgatory_start)

	/* Destination for this code i.e. end of memory to be swapped. */
	lg	%r9,crash_size-.base_crash(%r13)
	aghi	%r9,-(purgatory_end-purgatory_start)

	/* Destination in crash memory, i.e. same as r9 but in crash memory. */
	lg	%r10,crash_start-.base_crash(%r13)
	agr	%r10,%r9

	/* Buffer location (in crash memory) and size. As the purgatory is
	 * behind the point of no return it can re-use the stack as buffer.
	 */
	lghi	%r11,bufsz
	larl	%r12,stack

	MEMCPY	%r12,%r9,%r11	/* dst	-> (crash) buf */
	MEMCPY	%r9,%r8,%r11	/* self -> dst */

	/* Jump to new location. */
	lgr	%r7,%r9
	aghi	%r7,.jump_to_dst-purgatory_start
	br	%r7

.jump_to_dst:
	basr	%r13,0
.base_dst:

	/* clear buffer */
	MEMCPY	%r12,%r10,%r11	/* (crash) buf -> (crash) dst */

	/* Load new buffer location after jump */
	larl	%r7,stack
	aghi	%r10,stack-purgatory_start
	MEMCPY	%r10,%r7,%r11	/* (new) buf -> (crash) buf */

	/* Now the code is set up to run from its designated location. Start
	 * swapping the rest of crash memory now.
	 *
	 * The registers will be used as follow:
	 *
	 *	%r0-%r4	reserved for macros defined above
	 *	%r5-%r6 tmp registers
	 *	%r7	pointer to current struct sha region
	 *	%r8	index to iterate over all sha regions
	 *	%r9	pointer in crash memory
	 *	%r10	pointer in old kernel
	 *	%r11	total size (still) to be moved
	 *	%r12	pointer to buffer
	 */
	lgr	%r12,%r7
	lgr	%r11,%r9
	lghi	%r10,0
	lg	%r9,crash_start-.base_dst(%r13)
	lghi	%r8,16	/* KEXEC_SEGMENTS_MAX */
	larl	%r7,purgatory_sha_regions

	j .loop_first

	/* Loop over all purgatory_sha_regions. */
.loop_next:
	aghi	%r8,-1
	cghi	%r8,0
	je	.loop_out

	aghi	%r7,__KEXEC_SHA_REGION_SIZE

.loop_first:
	lg	%r5,__KEXEC_SHA_REGION_START(%r7)
	cghi	%r5,0
	je	.loop_next

	/* Copy [end last sha region, start current sha region) */
	/* Note: kexec_sha_region->start points in crash memory */
	sgr	%r5,%r9
	MEMCPY	%r9,%r10,%r5

	agr	%r9,%r5
	agr	%r10,%r5
	sgr	%r11,%r5

	/* Swap sha region */
	lg	%r6,__KEXEC_SHA_REGION_LEN(%r7)
	MEMSWAP	%r9,%r10,%r12,%r6
	sg	%r11,__KEXEC_SHA_REGION_LEN(%r7)
	j	.loop_next

.loop_out:
	/* Copy rest of crash memory */
	MEMCPY	%r9,%r10,%r11

	/* start crash kernel */
	START_NEXT_KERNEL .base_dst


load_psw_mask:
	.long	0x00080000,0x80000000

	.align	8
disabled_wait_psw:
	.quad	0x0002000180000000
	.quad	0x0000000000000000 + .do_checksum_verification

gprregs:
	.rept	10
	.quad	0
	.endr

/* Macro to define a global variable with name and size (in bytes) to be
 * shared with C code.
 *
 * Add the .size and .type attribute to satisfy checks on the Elf_Sym during
 * purgatory load.
 */
.macro GLOBAL_VARIABLE name,size
\name:
	.global \name
	.size	\name,\size
	.type	\name,object
	.skip	\size,0
.endm

GLOBAL_VARIABLE purgatory_sha256_digest,32
GLOBAL_VARIABLE purgatory_sha_regions,16*__KEXEC_SHA_REGION_SIZE
GLOBAL_VARIABLE kernel_entry,8
GLOBAL_VARIABLE kernel_type,8
GLOBAL_VARIABLE crash_start,8
GLOBAL_VARIABLE crash_size,8

	.align	PAGE_SIZE
stack:
	/* The buffer to move this code must be as big as the code. */
	.skip	stack-purgatory_start
	.align	PAGE_SIZE
purgatory_end:
