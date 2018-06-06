// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_FUTEX_H__
#define __NDS32_FUTEX_H__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_ex_table(err_reg)			\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 4f\n"				\
	"	.long	2b, 4f\n"				\
	"	.popsection\n"					\
	"	.pushsection .fixup,\"ax\"\n"			\
	"4:	move	%0, " err_reg "\n"			\
	"	j	3b\n"					\
	"	.popsection"

#define __futex_atomic_op(insn, ret, oldval, tmp, uaddr, oparg)	\
	smp_mb();						\
	asm volatile(					\
	"	movi	$ta, #0\n"				\
	"1:	llw	%1, [%2+$ta]\n"				\
	"	" insn "\n"					\
	"2:	scw	%0, [%2+$ta]\n"				\
	"	beqz	%0, 1b\n"				\
	"	movi	%0, #0\n"				\
	"3:\n"							\
	__futex_atomic_ex_table("%4")				\
	: "=&r" (ret), "=&r" (oldval)				\
	: "r" (uaddr), "r" (oparg), "i" (-EFAULT)		\
	: "cc", "memory")
static inline int
futex_atomic_cmpxchg_inatomic(u32 * uval, u32 __user * uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val, tmp, flags;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	smp_mb();
	asm volatile ("       movi    $ta, #0\n"
		      "1:     llw     %1, [%6 + $ta]\n"
		      "       sub     %3, %1, %4\n"
		      "       cmovz   %2, %5, %3\n"
		      "       cmovn   %2, %1, %3\n"
		      "2:     scw     %2, [%6 + $ta]\n"
		      "       beqz    %2, 1b\n"
		      "3:\n                   " __futex_atomic_ex_table("%7")
		      :"+&r"(ret), "=&r"(val), "=&r"(tmp), "=&r"(flags)
		      :"r"(oldval), "r"(newval), "r"(uaddr), "i"(-EFAULT)
		      :"$ta", "memory");
	smp_mb();

	*uval = val;
	return ret;
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;


	pagefault_disable();
	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("move	%0, %3", ret, oldval, tmp, uaddr,
				  oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add	%0, %1, %3", ret, oldval, tmp, uaddr,
				  oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or	%0, %1, %3", ret, oldval, tmp, uaddr,
				  oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	%0, %1, %3", ret, oldval, tmp, uaddr,
				  ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor	%0, %1, %3", ret, oldval, tmp, uaddr,
				  oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();

	if (!ret)
		*oval = oldval;

	return ret;
}
#endif /* __NDS32_FUTEX_H__ */
