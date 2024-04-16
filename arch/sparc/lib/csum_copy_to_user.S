/* SPDX-License-Identifier: GPL-2.0 */
/* csum_copy_to_user.S: Checksum+copy to userspace.
 *
 * Copyright (C) 2005 David S. Miller (davem@davemloft.net)
 */

#define EX_ST(x)		\
98:	x;			\
	.section .fixup,"ax";	\
	.align 4;		\
99:	retl;			\
	 mov	0, %o0;		\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

#define FUNC_NAME		csum_and_copy_to_user
#define STORE(type,src,addr)	type##a src, [addr] %asi

#include "csum_copy.S"
