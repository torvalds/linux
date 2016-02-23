/*
 * Add-on to transform csum_partial_copy_nocheck in checksumcopy.S into
 * csum_partial_copy_from_user by adding exception records.
 *
 * Copyright (C) 2001, 2003 Axis Communications AB.
 *
 * Author: Hans-Peter Nilsson.
 */

#include <asm/errno.h>

/* Same function body, but a different name.  If we just added exception
   records to _csum_partial_copy_nocheck and made it generic, we wouldn't
   know a user fault from a kernel fault and we would have overhead in
   each kernel caller for the error-pointer argument.

   unsigned int csum_partial_copy_from_user
     (const char *src, char *dst, int len, unsigned int sum, int *errptr);

   Note that the errptr argument is only set if we encounter an error.
   It is conveniently located on the stack, so the normal function body
   does not have to handle it.  */

#define csum_partial_copy_nocheck csum_partial_copy_from_user

/* There are local labels numbered 1, 2 and 3 present to mark the
   different from-user accesses.  */
#include "checksumcopy.S"

	.section .fixup,"ax"

;; Here from the movem loop; restore stack.
4:
	movem	[$sp+],$r8
;; r12 is already decremented.  Add back chunk_size-2.
	addq	40-2,$r12

;; Here from the word loop; r12 is off by 2; add it back.
5:
	addq	2,$r12

;; Here from a failing single byte.
6:

;; Signal in *errptr that we had a failing access.
	move.d	[$sp],$acr
	moveq	-EFAULT,$r9
	subq	4,$sp
	move.d	$r9,[$acr]

;; Clear the rest of the destination area using memset.  Preserve the
;; checksum for the readable bytes.
	move.d	$r13,[$sp]
	subq	4,$sp
	move.d	$r11,$r10
	move	$srp,[$sp]
	jsr	memset
	clear.d	$r11

	move	[$sp+],$srp
	ret
	move.d	[$sp+],$r10

	.previous
	.section __ex_table,"a"
	.dword 1b,4b
	.dword 2b,5b
	.dword 3b,6b
	.previous
