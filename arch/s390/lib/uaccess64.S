/*
 *  arch/s390x/lib/uaccess.S
 *    __copy_{from|to}_user functions.
 *
 *  s390
 *    Copyright (C) 2000,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Authors(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  These functions have standard call interface
 */

#include <linux/errno.h>
#include <asm/lowcore.h>
#include <asm/offsets.h>

        .text
        .align 4
        .globl __copy_from_user_asm
	# %r2 = to, %r3 = n, %r4 = from
__copy_from_user_asm:
	slgr	%r0,%r0
0:	mvcp	0(%r3,%r2),0(%r4),%r0
	jnz	1f
	slgr	%r2,%r2
	br	%r14
1:	la	%r2,256(%r2)
	la	%r4,256(%r4)
	aghi	%r3,-256
2:	mvcp	0(%r3,%r2),0(%r4),%r0
	jnz	1b
3:	slgr	%r2,%r2
	br	%r14
4:	lghi	%r0,-4096
	lgr	%r5,%r4
	slgr	%r5,%r0
	ngr	%r5,%r0		# %r5 = (%r4 + 4096) & -4096
	slgr	%r5,%r4		# %r5 = #bytes to next user page boundary
	clgr	%r3,%r5		# copy crosses next page boundary ?
	jnh	6f		# no, the current page faulted
	# move with the reduced length which is < 256
5:	mvcp	0(%r5,%r2),0(%r4),%r0
	slgr	%r3,%r5
6:	lgr	%r2,%r3
	br	%r14
        .section __ex_table,"a"
	.quad	0b,4b
	.quad	2b,4b
	.quad	5b,6b
        .previous

        .align 4
        .text
        .globl __copy_to_user_asm
	# %r2 = from, %r3 = n, %r4 = to
__copy_to_user_asm:
	slgr	%r0,%r0
0:	mvcs	0(%r3,%r4),0(%r2),%r0
	jnz	1f
	slgr	%r2,%r2
	br	%r14
1:	la	%r2,256(%r2)
	la	%r4,256(%r4)
	aghi	%r3,-256
2:	mvcs	0(%r3,%r4),0(%r2),%r0
	jnz	1b
3:	slgr	%r2,%r2
	br	%r14
4:	lghi	%r0,-4096
	lgr	%r5,%r4
	slgr	%r5,%r0
	ngr	%r5,%r0		# %r5 = (%r4 + 4096) & -4096
	slgr	%r5,%r4		# %r5 = #bytes to next user page boundary
	clgr	%r3,%r5		# copy crosses next page boundary ?
	jnh	6f		# no, the current page faulted
	# move with the reduced length which is < 256
5:	mvcs	0(%r5,%r4),0(%r2),%r0
	slgr	%r3,%r5
6:	lgr	%r2,%r3
	br	%r14
        .section __ex_table,"a"
	.quad	0b,4b
	.quad	2b,4b
	.quad	5b,6b
        .previous

        .align 4
        .text
        .globl __copy_in_user_asm
	# %r2 = from, %r3 = n, %r4 = to
__copy_in_user_asm:
	sacf	256
	bras	1,1f
	mvc	0(1,%r4),0(%r2)
0:	mvc	0(256,%r4),0(%r2)
	la	%r2,256(%r2)
	la	%r4,256(%r4)
1:	aghi	%r3,-256
	jnm	0b
2:	ex	%r3,0(%r1)
	sacf	0
	slgr	%r2,%r2
	br	14
3:	mvc	0(1,%r4),0(%r2)
	la	%r2,1(%r2)
	la	%r4,1(%r4)
	aghi	%r3,-1
	jnm	3b
4:	lgr	%r2,%r3
	sacf	0
	br	%r14
        .section __ex_table,"a"
	.quad	0b,3b
	.quad	2b,3b
	.quad	3b,4b
        .previous

        .align 4
        .text
        .globl __clear_user_asm
	# %r2 = to, %r3 = n
__clear_user_asm:
	slgr	%r0,%r0
	larl	%r5,empty_zero_page
1:	mvcs	0(%r3,%r2),0(%r5),%r0
	jnz	2f
	slgr	%r2,%r2
	br	%r14
2:	la	%r2,256(%r2)
	aghi	%r3,-256
3:	mvcs	0(%r3,%r2),0(%r5),%r0
	jnz	2b
4:	slgr	%r2,%r2
	br	%r14
5:	lghi	%r0,-4096
	lgr	%r4,%r2
	slgr	%r4,%r0
	ngr	%r4,%r0		# %r4 = (%r2 + 4096) & -4096
	slgr	%r4,%r2		# %r4 = #bytes to next user page boundary
	clgr	%r3,%r4		# clear crosses next page boundary ?
	jnh	7f		# no, the current page faulted
	# clear with the reduced length which is < 256
6:	mvcs	0(%r4,%r2),0(%r5),%r0
	slgr	%r3,%r4
7:	lgr	%r2,%r3
	br	%r14
        .section __ex_table,"a"
	.quad	1b,5b
	.quad	3b,5b
	.quad	6b,7b
        .previous

        .align 4
        .text
        .globl __strncpy_from_user_asm
	# %r2 = count, %r3 = dst, %r4 = src
__strncpy_from_user_asm:
	lghi	%r0,0
	lgr	%r1,%r4
	la	%r2,0(%r2,%r4)	# %r2 points to first byte after string
	sacf	256
0:	srst	%r2,%r1
	jo	0b
	sacf	0
	lgr	%r1,%r2
	jh	1f		# \0 found in string ?
	aghi	%r1,1		# include \0 in copy
1:	slgr	%r1,%r4		# %r1 = copy length (without \0)
	slgr	%r2,%r4		# %r2 = return length (including \0)
2:	mvcp	0(%r1,%r3),0(%r4),%r0
	jnz	3f
	br	%r14
3:	la	%r3,256(%r3)
	la	%r4,256(%r4)
	aghi	%r1,-256
	mvcp	0(%r1,%r3),0(%r4),%r0
	jnz	3b
	br	%r14
4:	sacf	0
	lghi	%r2,-EFAULT
	br	%r14
	.section __ex_table,"a"
	.quad	0b,4b
	.previous

        .align 4
        .text
        .globl __strnlen_user_asm
	# %r2 = count, %r3 = src
__strnlen_user_asm:
	lghi	%r0,0
	lgr	%r1,%r3
	la	%r2,0(%r2,%r3)	# %r2 points to first byte after string
	sacf	256
0:	srst	%r2,%r1
	jo	0b
	sacf	0
	jh	1f		# \0 found in string ?
	aghi	%r2,1		# strnlen_user result includes the \0
1:	slgr	%r2,%r3
	br	%r14
2:	sacf	0
	lghi	%r2,-EFAULT
	br	%r14
	.section __ex_table,"a"
	.quad	0b,2b
	.previous
