/* U3copy_from_user.S: UltraSparc-III optimized copy from userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#define EX_LD(x)		\
98:	x;			\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, __retl_one;	\
	.text;			\
	.align 4;

#define FUNC_NAME		U3copy_from_user
#define LOAD(type,addr,dest)	type##a [addr] %asi, dest
#define EX_RETVAL(x)		0

#include "U3memcpy.S"
