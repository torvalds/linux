/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_HEXAGON_FUTEX_H
#define _ASM_HEXAGON_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

/* XXX TODO-- need to add sync barriers! */

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
	__asm__ __volatile( \
	"1: %0 = memw_locked(%3);\n" \
	    /* For example: %1 = %4 */ \
	    insn \
	"2: memw_locked(%3,p2) = %1;\n" \
	"   if !p2 jump 1b;\n" \
	"   %1 = #0;\n" \
	"3:\n" \
	".section .fixup,\"ax\"\n" \
	"4: %1 = #%5;\n" \
	"   jump 3b\n" \
	".previous\n" \
	".section __ex_table,\"a\"\n" \
	".long 1b,4b,2b,4b\n" \
	".previous\n" \
	: "=&r" (oldval), "=&r" (ret), "+m" (*uaddr) \
	: "r" (uaddr), "r" (oparg), "i" (-EFAULT) \
	: "p2", "memory")


static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("%1 = %4\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("%1 = add(%0,%4)\n", ret, oldval, uaddr,
				  oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("%1 = or(%0,%4)\n", ret, oldval, uaddr,
				  oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("%1 = not(%4); %1 = and(%0,%1)\n", ret,
				  oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("%1 = xor(%0,%4)\n", ret, oldval, uaddr,
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

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval,
			      u32 newval)
{
	int prev;
	int ret;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	__asm__ __volatile__ (
	"1: %1 = memw_locked(%3)\n"
	"   {\n"
	"      p2 = cmp.eq(%1,%4)\n"
	"      if !p2.new jump:NT 3f\n"
	"   }\n"
	"2: memw_locked(%3,p2) = %5\n"
	"   if !p2 jump 1b\n"
	"3:\n"
	".section .fixup,\"ax\"\n"
	"4: %0 = #%6\n"
	"   jump 3b\n"
	".previous\n"
	".section __ex_table,\"a\"\n"
	".long 1b,4b,2b,4b\n"
	".previous\n"
	: "+r" (ret), "=&r" (prev), "+m" (*uaddr)
	: "r" (uaddr), "r" (oldval), "r" (newval), "i"(-EFAULT)
	: "p2", "memory");

	*uval = prev;
	return ret;
}

#endif /* __KERNEL__ */
#endif /* _ASM_HEXAGON_FUTEX_H */
