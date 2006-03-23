/* NGcopy_from_user.S: Niagara optimized copy from userspace.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#define EX_LD(x)		\
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

#define FUNC_NAME		NGcopy_from_user
#define LOAD(type,addr,dest)	type##a [addr] ASI_AIUS, dest
#define LOAD_TWIN(addr_reg,dest0,dest1)	\
	ldda [addr_reg] ASI_BLK_INIT_QUAD_LDD_AIUS, dest0
#define EX_RETVAL(x)		0

#ifdef __KERNEL__
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, memcpy_user_stub;		\
	 nop
#endif

#include "NGmemcpy.S"
