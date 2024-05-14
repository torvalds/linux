/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_FUTEX_H
#define __ASM_CSKY_FUTEX_H

#ifndef CONFIG_SMP
#include <asm-generic/futex.h>
#else
#include <linux/atomic.h>
#include <linux/futex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)		\
{									\
	u32 tmp;							\
									\
	__atomic_pre_full_fence();					\
									\
	__asm__ __volatile__ (						\
	"1:	ldex.w	%[ov], %[u]			\n"		\
	"	"insn"					\n"		\
	"2:	stex.w	%[t], %[u]			\n"		\
	"	bez	%[t], 1b			\n"		\
	"	br	4f				\n"		\
	"3:	mov	%[r], %[e]			\n"		\
	"4:						\n"		\
	"	.section __ex_table,\"a\"		\n"		\
	"	.balign 4				\n"		\
	"	.long	1b, 3b				\n"		\
	"	.long	2b, 3b				\n"		\
	"	.previous				\n"		\
	: [r] "+r" (ret), [ov] "=&r" (oldval),				\
	  [u] "+m" (*uaddr), [t] "=&r" (tmp)				\
	: [op] "Jr" (oparg), [e] "jr" (-EFAULT)				\
	: "memory");							\
									\
	__atomic_post_full_fence();					\
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret = 0;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov %[t], %[ov]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add %[t], %[ov], %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %[t], %[ov], %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and %[t], %[ov], %[op]",
				  ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %[t], %[ov], %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	if (!ret)
		*oval = oldval;

	return ret;
}



static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val, tmp;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	__atomic_pre_full_fence();

	__asm__ __volatile__ (
	"1:	ldex.w	%[v], %[u]			\n"
	"	cmpne	%[v], %[ov]			\n"
	"	bt	4f				\n"
	"	mov	%[t], %[nv]			\n"
	"2:	stex.w	%[t], %[u]			\n"
	"	bez	%[t], 1b			\n"
	"	br	4f				\n"
	"3:	mov	%[r], %[e]			\n"
	"4:						\n"
	"	.section __ex_table,\"a\"		\n"
	"	.balign 4				\n"
	"	.long	1b, 3b				\n"
	"	.long	2b, 3b				\n"
	"	.previous				\n"
	: [r] "+r" (ret), [v] "=&r" (val), [u] "+m" (*uaddr),
	  [t] "=&r" (tmp)
	: [ov] "Jr" (oldval), [nv] "Jr" (newval), [e] "Jr" (-EFAULT)
	: "memory");

	__atomic_post_full_fence();

	*uval = val;
	return ret;
}

#endif /* CONFIG_SMP */
#endif /* __ASM_CSKY_FUTEX_H */
