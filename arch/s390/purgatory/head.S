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

/* The purgatory is the code running between two kernels. It's main purpose
 * is to verify that the next kernel was not corrupted after load and to
 * start it.
 */

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
	larl	%r15,purgatory_end
	aghi	%r15,-160

.do_checksum_verification:
	brasl	%r14,verify_sha256_digest

	cghi	%r2,0		/* checksum match */
	jne	.disabled_wait

	/* start normal kernel */
	START_NEXT_KERNEL .base_crash

.disabled_wait:
	lpswe	disabled_wait_psw-.base_crash(%r13)


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

purgatory_sha256_digest:
	.global purgatory_sha256_digest
	.rept	32	/* SHA256_DIGEST_SIZE */
	.byte	0
	.endr

purgatory_sha_regions:
	.global purgatory_sha_regions
	.rept	16 * __KEXEC_SHA_REGION_SIZE	/* KEXEC_SEGMENTS_MAX */
	.byte	0
	.endr

kernel_entry:
	.global kernel_entry
	.quad	0

	.align	PAGE_SIZE
stack:
	.skip	PAGE_SIZE
	.align	PAGE_SIZE
purgatory_end:
