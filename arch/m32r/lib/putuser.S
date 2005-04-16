/*
 * __put_user functions.
 *
 * (C) Copyright 1998 Linus Torvalds
 * (C) Copyright 2001 Hirokazu Takata
 *
 * These functions have a non-standard call interface
 * to make them more efficient.
 */

#include <linux/config.h>

/*
 * __put_user_X
 *
 * Inputs:	r0 contains the address
 *		r1 contains the value
 *
 * Outputs:	r0 is error code (0 or -EFAULT)
 *		r1 is corrupted (will contain "current_task").
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#ifdef CONFIG_ISA_DUAL_ISSUE

	.text
	.balign 4
	.globl __put_user_1
__put_user_1:
1:	stb	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __put_user_2
__put_user_2:
2:	sth	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __put_user_4
__put_user_4:
3:	st	r1, @r0		    ||	ldi	r0, #0
	jmp	r14

bad_put_user:
	ldi	r0, #-14	    ||	jmp	r14

#else /* not CONFIG_ISA_DUAL_ISSUE */

	.text
	.balign 4
	.globl __put_user_1
__put_user_1:
1:	stb	r1, @r0
	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __put_user_2
__put_user_2:
2:	sth	r1, @r0
	ldi	r0, #0
	jmp	r14

	.balign 4
	.globl __put_user_4
__put_user_4:
3:	st	r1, @r0
	ldi	r0, #0
	jmp	r14

bad_put_user:
	ldi	r0, #-14
	jmp	r14

#endif /* not CONFIG_ISA_DUAL_ISSUE */

.section __ex_table,"a"
	.long 1b,bad_put_user
	.long 2b,bad_put_user
	.long 3b,bad_put_user
.previous
