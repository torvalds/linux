/*
 * M7copy_to_user.S: SPARC M7 optimized copy to userspace.
 *
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 */


#define EX_ST(x, y)			\
98:	x;				\
	.section __ex_table,"a";	\
	.align 4;			\
	.word 98b, y;			\
	.text;				\
	.align 4;

#define EX_ST_FP(x, y)			\
98:	x;				\
	.section __ex_table,"a";	\
	.align 4;			\
	.word 98b, y##_fp;		\
	.text;				\
	.align 4;


#ifndef ASI_AIUS
#define ASI_AIUS	0x11
#endif

#ifndef ASI_BLK_INIT_QUAD_LDD_AIUS
#define ASI_BLK_INIT_QUAD_LDD_AIUS 0x23
#endif

#define FUNC_NAME		M7copy_to_user
#define STORE(type,src,addr)	type##a src, [addr] %asi
#define STORE_ASI		ASI_BLK_INIT_QUAD_LDD_AIUS
#define	STORE_MRU_ASI		ASI_ST_BLKINIT_MRU_S
#define EX_RETVAL(x)		0

#ifdef __KERNEL__
	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, raw_copy_in_user;		\
	 nop
#endif

#include "M7memcpy.S"
