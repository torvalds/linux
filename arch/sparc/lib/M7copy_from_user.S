/*
 * M7copy_from_user.S: SPARC M7 optimized copy from userspace.
 *
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 */


#define EX_LD(x, y)			\
98:	x;				\
	.section __ex_table,"a";	\
	.align 4;			\
	.word 98b, y;			\
	.text;				\
	.align 4;

#define EX_LD_FP(x, y)			\
98:	x;				\
	.section __ex_table,"a";	\
	.align 4;			\
	.word 98b, y##_fp;		\
	.text;				\
	.align 4;

#ifndef ASI_AIUS
#define ASI_AIUS	0x11
#endif

#define FUNC_NAME		M7copy_from_user
#define LOAD(type,addr,dest)	type##a [addr] %asi, dest
#define EX_RETVAL(x)		0

#ifdef __KERNEL__
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, raw_copy_in_user;		\
	 nop
#endif

#include "M7memcpy.S"
