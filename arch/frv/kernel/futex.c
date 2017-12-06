/* futex.c: futex operations
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/futex.h>
#include <asm/errno.h>

/*
 * the various futex operations; MMU fault checking is ignored under no-MMU
 * conditions
 */
static inline int atomic_futex_op_xchg_set(int oparg, u32 __user *uaddr, int *_oldval)
{
	int oldval, ret;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "1:	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "2:	cst.p		%3,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    "	setlos		0,%2			\n"
	    "3:						\n"
	    ".subsection 2				\n"
	    "4:	setlos		%5,%2			\n"
	    "	bra		3b			\n"
	    ".previous					\n"
	    ".section __ex_table,\"a\"			\n"
	    "	.balign		8			\n"
	    "	.long		1b,4b			\n"
	    "	.long		2b,4b			\n"
	    ".previous"
	    : "+U"(*uaddr), "=&r"(oldval), "=&r"(ret), "=r"(oparg)
	    : "3"(oparg), "i"(-EFAULT)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	*_oldval = oldval;
	return ret;
}

static inline int atomic_futex_op_xchg_add(int oparg, u32 __user *uaddr, int *_oldval)
{
	int oldval, ret;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "1:	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	add		%1,%3,%3		\n"
	    "2:	cst.p		%3,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    "	setlos		0,%2			\n"
	    "3:						\n"
	    ".subsection 2				\n"
	    "4:	setlos		%5,%2			\n"
	    "	bra		3b			\n"
	    ".previous					\n"
	    ".section __ex_table,\"a\"			\n"
	    "	.balign		8			\n"
	    "	.long		1b,4b			\n"
	    "	.long		2b,4b			\n"
	    ".previous"
	    : "+U"(*uaddr), "=&r"(oldval), "=&r"(ret), "=r"(oparg)
	    : "3"(oparg), "i"(-EFAULT)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	*_oldval = oldval;
	return ret;
}

static inline int atomic_futex_op_xchg_or(int oparg, u32 __user *uaddr, int *_oldval)
{
	int oldval, ret;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "1:	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	or		%1,%3,%3		\n"
	    "2:	cst.p		%3,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    "	setlos		0,%2			\n"
	    "3:						\n"
	    ".subsection 2				\n"
	    "4:	setlos		%5,%2			\n"
	    "	bra		3b			\n"
	    ".previous					\n"
	    ".section __ex_table,\"a\"			\n"
	    "	.balign		8			\n"
	    "	.long		1b,4b			\n"
	    "	.long		2b,4b			\n"
	    ".previous"
	    : "+U"(*uaddr), "=&r"(oldval), "=&r"(ret), "=r"(oparg)
	    : "3"(oparg), "i"(-EFAULT)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	*_oldval = oldval;
	return ret;
}

static inline int atomic_futex_op_xchg_and(int oparg, u32 __user *uaddr, int *_oldval)
{
	int oldval, ret;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "1:	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	and		%1,%3,%3		\n"
	    "2:	cst.p		%3,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    "	setlos		0,%2			\n"
	    "3:						\n"
	    ".subsection 2				\n"
	    "4:	setlos		%5,%2			\n"
	    "	bra		3b			\n"
	    ".previous					\n"
	    ".section __ex_table,\"a\"			\n"
	    "	.balign		8			\n"
	    "	.long		1b,4b			\n"
	    "	.long		2b,4b			\n"
	    ".previous"
	    : "+U"(*uaddr), "=&r"(oldval), "=&r"(ret), "=r"(oparg)
	    : "3"(oparg), "i"(-EFAULT)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	*_oldval = oldval;
	return ret;
}

static inline int atomic_futex_op_xchg_xor(int oparg, u32 __user *uaddr, int *_oldval)
{
	int oldval, ret;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "1:	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	xor		%1,%3,%3		\n"
	    "2:	cst.p		%3,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    "	setlos		0,%2			\n"
	    "3:						\n"
	    ".subsection 2				\n"
	    "4:	setlos		%5,%2			\n"
	    "	bra		3b			\n"
	    ".previous					\n"
	    ".section __ex_table,\"a\"			\n"
	    "	.balign		8			\n"
	    "	.long		1b,4b			\n"
	    "	.long		2b,4b			\n"
	    ".previous"
	    : "+U"(*uaddr), "=&r"(oldval), "=&r"(ret), "=r"(oparg)
	    : "3"(oparg), "i"(-EFAULT)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	*_oldval = oldval;
	return ret;
}

/*****************************************************************************/
/*
 * do the futex operations
 */
int arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		ret = atomic_futex_op_xchg_set(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ADD:
		ret = atomic_futex_op_xchg_add(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_OR:
		ret = atomic_futex_op_xchg_or(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ANDN:
		ret = atomic_futex_op_xchg_and(~oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_XOR:
		ret = atomic_futex_op_xchg_xor(oparg, uaddr, &oldval);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	pagefault_enable();

	if (!ret)
		*oval = oldval;

	return ret;

} /* end arch_futex_atomic_op_inuser() */
