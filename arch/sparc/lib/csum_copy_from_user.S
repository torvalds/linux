/* csum_copy_from_user.S: Checksum+copy from userspace.
 *
 * Copyright (C) 2005 David S. Miller (davem@davemloft.net)
 */

#define EX_LD(x)		\
98:	x;			\
	.section .fixup, "ax";	\
	.align 4;		\
99:	retl;			\
	 mov	-1, %o0;	\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

#define FUNC_NAME		__csum_partial_copy_from_user
#define LOAD(type,addr,dest)	type##a [addr] %asi, dest

#include "csum_copy.S"
