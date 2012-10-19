/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_FUTEX_H
#define __ASM_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, tmp, oparg)		\
	asm volatile(							\
"1:	ldaxr	%w1, %2\n"						\
	insn "\n"							\
"2:	stlxr	%w3, %w0, %2\n"						\
"	cbnz	%w3, 1b\n"						\
"3:\n"									\
"	.pushsection .fixup,\"ax\"\n"					\
"4:	mov	%w0, %w5\n"						\
"	b	3b\n"							\
"	.popsection\n"							\
"	.pushsection __ex_table,\"a\"\n"				\
"	.align	3\n"							\
"	.quad	1b, 4b, 2b, 4b\n"					\
"	.popsection\n"							\
	: "=&r" (ret), "=&r" (oldval), "+Q" (*uaddr), "=&r" (tmp)	\
	: "r" (oparg), "Ir" (-EFAULT)					\
	: "cc")

static inline int
futex_atomic_op_inuser (int encoded_op, u32 __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret, tmp;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	pagefault_disable();	/* implies preempt_disable() */

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov	%w0, %w4",
				  ret, oldval, uaddr, tmp, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add	%w0, %w1, %w4",
				  ret, oldval, uaddr, tmp, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("orr	%w0, %w1, %w4",
				  ret, oldval, uaddr, tmp, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	%w0, %w1, %w4",
				  ret, oldval, uaddr, tmp, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("eor	%w0, %w1, %w4",
				  ret, oldval, uaddr, tmp, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();	/* subsumes preempt_enable() */

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ: ret = (oldval == cmparg); break;
		case FUTEX_OP_CMP_NE: ret = (oldval != cmparg); break;
		case FUTEX_OP_CMP_LT: ret = (oldval < cmparg); break;
		case FUTEX_OP_CMP_GE: ret = (oldval >= cmparg); break;
		case FUTEX_OP_CMP_LE: ret = (oldval <= cmparg); break;
		case FUTEX_OP_CMP_GT: ret = (oldval > cmparg); break;
		default: ret = -ENOSYS;
		}
	}
	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val, tmp;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	asm volatile("// futex_atomic_cmpxchg_inatomic\n"
"1:	ldaxr	%w1, %2\n"
"	sub	%w3, %w1, %w4\n"
"	cbnz	%w3, 3f\n"
"2:	stlxr	%w3, %w5, %2\n"
"	cbnz	%w3, 1b\n"
"3:\n"
"	.pushsection .fixup,\"ax\"\n"
"4:	mov	%w0, %w6\n"
"	b	3b\n"
"	.popsection\n"
"	.pushsection __ex_table,\"a\"\n"
"	.align	3\n"
"	.quad	1b, 4b, 2b, 4b\n"
"	.popsection\n"
	: "+r" (ret), "=&r" (val), "+Q" (*uaddr), "=&r" (tmp)
	: "r" (oldval), "r" (newval), "Ir" (-EFAULT)
	: "cc", "memory");

	*uval = val;
	return ret;
}

#endif /* __KERNEL__ */
#endif /* __ASM_FUTEX_H */
