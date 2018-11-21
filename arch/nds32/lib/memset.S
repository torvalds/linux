// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>

	.text
ENTRY(memset)
	move	$r5, $r0		! Return value
	beqz	$r2, end_memset		! Exit when len = 0
	srli	$p1, $r2, 2		! $p1 is how many words to copy
	andi	$r2, $r2, 3		! How many bytes are less than a word
	beqz	$p1, byte_set		! When n is less than a word

	! set $r1 from ??????ab to abababab
	andi	$r1, $r1, #0x00ff	! $r1 = 000000ab
	slli	$p0, $r1, #8		! $p0 = 0000ab00
	or	$r1, $r1, $p0		! $r1 = 0000abab
	slli	$p0, $r1, #16		! $p0 = abab0000
	or	$r1, $r1, $p0		! $r1 = abababab
word_set:
	addi	$p1, $p1, #-1		! How many words left to copy
	smw.bim	$r1, [$r0], $r1		! Copy the word to det
	bnez	$p1, word_set		! Still words to set, continue looping
	beqz	$r2, end_memset		! No left byte to set
byte_set:				! Less than 4 bytes left to set
	addi	$r2, $r2, #-1		! Decrease len by 1
	sbi.bi	$r1, [$r0], #1		! Set data of the next byte to $r1
	bnez	$r2, byte_set		! Still bytes left to set
end_memset:
	move	$r0, $r5
	ret

ENDPROC(memset)
