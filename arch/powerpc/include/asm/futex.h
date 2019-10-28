/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_FUTEX_H
#define _ASM_POWERPC_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>
#include <asm/synch.h>
#include <asm/asm-405.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
  __asm__ __volatile ( \
	PPC_ATOMIC_ENTRY_BARRIER \
"1:	lwarx	%0,0,%2\n" \
	insn \
	PPC405_ERR77(0, %2) \
"2:	stwcx.	%1,0,%2\n" \
	"bne-	1b\n" \
	PPC_ATOMIC_EXIT_BARRIER \
	"li	%1,0\n" \
"3:	.section .fixup,\"ax\"\n" \
"4:	li	%1,%3\n" \
	"b	3b\n" \
	".previous\n" \
	EX_TABLE(1b, 4b) \
	EX_TABLE(2b, 4b) \
	: "=&r" (oldval), "=&r" (ret) \
	: "b" (uaddr), "i" (-EFAULT), "r" (oparg) \
	: "cr0", "memory")

static inline int arch_futex_atomic_op_inuser(int op, int oparg, int *oval,
		u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mr %1,%4\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add %1,%0,%4\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %1,%0,%4\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("andc %1,%0,%4\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %1,%0,%4\n", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();

	*oval = oldval;

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 prev;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

        __asm__ __volatile__ (
        PPC_ATOMIC_ENTRY_BARRIER
"1:     lwarx   %1,0,%3         # futex_atomic_cmpxchg_inatomic\n\
        cmpw    0,%1,%4\n\
        bne-    3f\n"
        PPC405_ERR77(0,%3)
"2:     stwcx.  %5,0,%3\n\
        bne-    1b\n"
        PPC_ATOMIC_EXIT_BARRIER
"3:	.section .fixup,\"ax\"\n\
4:	li	%0,%6\n\
	b	3b\n\
	.previous\n"
	EX_TABLE(1b, 4b)
	EX_TABLE(2b, 4b)
        : "+r" (ret), "=&r" (prev), "+m" (*uaddr)
        : "r" (uaddr), "r" (oldval), "r" (newval), "i" (-EFAULT)
        : "cc", "memory");

	*uval = prev;
        return ret;
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_FUTEX_H */
