/* U1copy_from_user.S: UltraSparc-I/II/IIi/IIe optimized copy from userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#define EX_LD(x)		\
98:	x;			\
	.section .fixup;	\
	.align 4;		\
99:	retl;			\
	 mov	1, %o0;		\
	.section __ex_table;	\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

#define FUNC_NAME		___copy_from_user
#define LOAD(type,addr,dest)	type##a [addr] %asi, dest
#define LOAD_BLK(addr,dest)	ldda [addr] ASI_BLK_AIUS, dest
#define EX_RETVAL(x)		0

	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, memcpy_user_stub;		\
	 nop;						\

#include "U1memcpy.S"
