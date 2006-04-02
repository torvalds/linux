/* NGcopy_to_user.S: Niagara optimized copy to userspace.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#define EX_ST(x)		\
98:	x;			\
	.section .fixup;	\
	.align 4;		\
99:	wr	%g0, ASI_AIUS, %asi;\
	retl;			\
	 mov	1, %o0;		\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

#ifndef ASI_AIUS
#define ASI_AIUS	0x11
#endif

#define FUNC_NAME		NGcopy_to_user
#define STORE(type,src,addr)	type##a src, [addr] ASI_AIUS
#define STORE_ASI		ASI_BLK_INIT_QUAD_LDD_AIUS
#define EX_RETVAL(x)		0

#ifdef __KERNEL__
	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, memcpy_user_stub;		\
	 nop
#endif

#include "NGmemcpy.S"
