/* U1copy_from_user.S: UltraSparc-I/II/IIi/IIe optimized copy from userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#define EX_LD(x,y)		\
98:	x;			\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, y;		\
	.text;			\
	.align 4;

#define EX_LD_FP(x,y)		\
98:	x;			\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, y;		\
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
	bne,pn		%icc, ___copy_in_user;		\
	 nop;						\

#include "U1memcpy.S"
