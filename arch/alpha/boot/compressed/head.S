/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Entry point for the self-extracting compressed kernel stub. aboot
 * jumps here with the OSF/1 parameter page (ZERO_PGE) already populated
 * and $30 set to INIT_STACK, exactly as it would for an uncompressed
 * vmlinux.
 */

#include <linux/init.h>
#include <asm/pal.h>

	.set noreorder

__HEAD
	.globl	__start
	.ent	__start
__start:
	br	$29,1f
1:	ldgp	$29,0($29)

	/*
	 * Clear our own bss. The decompressors keep state there, and
	 * nothing guarantees that aboot zeroed the part of our PT_LOAD
	 * segment that has no file backing.
	 */
	lda	$1,_bss
	lda	$2,_ebss
2:	cmpult	$1,$2,$3
	beq	$3,3f
	stq	$31,0($1)
	addq	$1,8,$1
	br	$31,2b

3:	jsr	$26,decompress_and_boot
	call_pal PAL_halt
	.end __start
