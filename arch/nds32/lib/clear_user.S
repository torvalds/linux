// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
	.text
	.align	5
ENTRY(__arch_clear_user)
	add	$r5, $r0, $r1
	beqz	$r1, clear_exit
	xor	$p1, $p1, $p1		! Use $p1=0 to clear mem
	srli	$p0, $r1, #2		! $p0 = number of word to clear
	andi	$r1, $r1, #3		! Bytes less than a word to copy
	beqz	$p0, byte_clear		! Only less than a word to clear
word_clear:
USER(	smw.bim,$p1, [$r0], $p1)	! Clear the word
	addi	$p0, $p0, #-1		! Decrease word count
	bnez	$p0, word_clear		! Continue looping to clear all words
	beqz	$r1, clear_exit		! No left bytes to copy
byte_clear:
USER(	sbi.bi,	$p1, [$r0], #1)		! Clear the byte
	addi	$r1, $r1, #-1		! Decrease byte count
	bnez	$r1, byte_clear		! Continue looping to clear all left bytes
clear_exit:
	move	$r0, $r1		! Set return value
	ret

	.section .fixup,"ax"
	.align	0
9001:
	sub	$r0, $r5, $r0		! Bytes left to copy
	ret
	.previous
ENDPROC(__arch_clear_user)
