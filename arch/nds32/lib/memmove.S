// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>

/*
  void *memmove(void *dst, const void *src, int n);

  dst: $r0
  src: $r1
  n  : $r2
  ret: $r0 - pointer to the memory area dst.
*/
	.text

ENTRY(memmove)
	move	$r5, $r0		! Set return value = det
	beq	$r0, $r1, exit_memcpy	! Exit when det = src
	beqz	$r2, exit_memcpy	! Exit when n = 0
	pushm	$t0, $t1		! Save reg
	srli	$p1, $r2, #2		! $p1 is how many words to copy

	! Avoid data lost when memory overlap
	! Copy data reversely when src < dst
	slt	$p0, $r0, $r1		! check if $r0 < $r1
	beqz	$p0, do_reverse		! branch if dst > src

	! No reverse, dst < src
	andi	$r2, $r2, #3		! How many bytes are less than a word
	li	$t0, #1			! Determining copy direction in byte_cpy
	beqz	$p1, byte_cpy		! When n is less than a word

word_cpy:
	lmw.bim	$p0, [$r1], $p0		! Read a word from src
	addi	$p1, $p1, #-1		! How many words left to copy
	smw.bim	$p0, [$r0], $p0		! Copy the word to det
	bnez	$p1, word_cpy		! If remained words > 0
	beqz	$r2, end_memcpy		! No left bytes to copy
	b	byte_cpy

do_reverse:
	add	$r0, $r0, $r2		! Start with the end of $r0
	add	$r1, $r1, $r2		! Start with the end of $r1
	andi	$r2, $r2, #3		! How many bytes are less than a word
	li	$t0, #-1		! Determining copy direction in byte_cpy
	beqz	$p1, reverse_byte_cpy	! When n is less than a word

reverse_word_cpy:
	lmw.adm	$p0, [$r1], $p0		! Read a word from src
	addi	$p1, $p1, #-1		! How many words left to copy
	smw.adm	$p0, [$r0], $p0		! Copy the word to det
	bnez	$p1, reverse_word_cpy	! If remained words > 0
	beqz	$r2, end_memcpy		! No left bytes to copy

reverse_byte_cpy:
	addi	$r0, $r0, #-1
	addi	$r1, $r1, #-1
byte_cpy:				! Less than 4 bytes to copy now
	lb.bi	$p0, [$r1], $t0		! Read a byte from src
	addi	$r2, $r2, #-1		! How many bytes left to copy
	sb.bi	$p0, [$r0], $t0		! copy the byte to det
	bnez	$r2, byte_cpy		! If remained bytes > 0

end_memcpy:
	popm	$t0, $t1
exit_memcpy:
	move	$r0, $r5
	ret

ENDPROC(memmove)
