// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation


	beq	$r1, $r0, quit_memcpy
	beqz	$r2, quit_memcpy
	srli    $r3, $r2, #5	! check if len < cache-line size 32
	beqz	$r3, word_copy_entry
	andi	$r4, $r0, #0x3	! check byte-align
	beqz	$r4, unalign_word_copy_entry

	addi	$r4, $r4,#-4
	abs	$r4, $r4	! check how many un-align byte to copy
	sub	$r2, $r2, $r4	! update $R2

unalign_byte_copy:
	lbi1	$r3, $r1, #1
	addi	$r4, $r4, #-1
	sbi1	$r3, $r0, #1
	bnez	$r4, unalign_byte_copy
	beqz	$r2, quit_memcpy

unalign_word_copy_entry:
	andi	$r3, $r0, 0x1f	! check cache-line unaligncount
	beqz	$r3, cache_copy

	addi	$r3, $r3, #-32
	abs	$r3, $r3
	sub	$r2, $r2, $r3	! update $R2

unalign_word_copy:
	lmw1	$r4, $r1, $r4
	addi	$r3, $r3, #-4
	smw1	$r4, $r0, $r4
	bnez	$r3, unalign_word_copy
	beqz	$r2, quit_memcpy

	addi	$r3, $r2, #-32	! to check $r2< cache_line , than go to word_copy
	bltz	$r3, word_copy_entry
cache_copy:
	srli	$r3, $r2, #5
	beqz	$r3, word_copy_entry
3:
	lmw1	$r17, $r1, $r24
	addi	$r3, $r3, #-1
	smw1	$r17, $r0, $r24
	bnez	$r3, 3b

word_copy_entry:
	andi	$r2, $r2, #31

	beqz	$r2, quit_memcpy
5:
	srli	$r3, $r2, #2
	beqz	$r3, byte_copy
word_copy:
	lmw1	$r4, $r1, $r4
	addi	$r3, $r3, #-1
	smw1	$r4, $r0, $r4
	bnez	$r3, word_copy
	andi	$r2, $r2, #3
	beqz	$r2, quit_memcpy
byte_copy:
	lbi1	$r3, $r1, #1
	addi	$r2, $r2, #-1

	sbi1	$r3, $r0, #1
	bnez	$r2, byte_copy
quit_memcpy:
