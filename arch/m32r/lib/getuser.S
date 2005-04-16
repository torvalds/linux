/*
 * __get_user functions.
 *
 * (C) Copyright 2001 Hirokazu Takata
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */

#include <linux/config.h>

/*
 * __get_user_X
 *
 * Inputs:	r0 contains the address
 *
 * Outputs:	r0 is error code (0 or -EFAULT)
 *		r1 contains zero-extended value
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#ifdef CONFIG_ISA_DUAL_ISSUE

	.text
	.balign 4
	.globl __get_user_1
__get_user_1:
1:	ldub	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __get_user_2
__get_user_2:
2:	lduh	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __get_user_4
__get_user_4:
3:	ld	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

bad_get_user:
	ldi	r1, #0		    ||	ldi	r0, #-14
	jmp	r14

#else /* not CONFIG_ISA_DUAL_ISSUE */

	.text
	.balign 4
	.globl __get_user_1
__get_user_1:
1:	ldub	r1, @r0
	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __get_user_2
__get_user_2:
2:	lduh	r1, @r0
	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __get_user_4
__get_user_4:
3:	ld	r1, @r0
	ldi	r0, #0
	jmp	r14

bad_get_user:
	ldi	r1, #0
	ldi	r0, #-14
	jmp	r14

#endif /* not CONFIG_ISA_DUAL_ISSUE */

.section __ex_table,"a"
	.long 1b,bad_get_user
	.long 2b,bad_get_user
	.long 3b,bad_get_user
.previous

	.end
