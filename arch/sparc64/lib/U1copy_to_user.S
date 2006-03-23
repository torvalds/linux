/* U1copy_to_user.S: UltraSparc-I/II/IIi/IIe optimized copy to userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#define EX_ST(x)		\
98:	x;			\
	.section .fixup;	\
	.align 4;		\
99:	retl;			\
	 mov	1, %o0;		\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

#define FUNC_NAME		___copy_to_user
#define STORE(type,src,addr)	type##a src, [addr] ASI_AIUS
#define STORE_BLK(src,addr)	stda src, [addr] ASI_BLK_AIUS
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
