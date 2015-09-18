#ifndef _ASM_S390_FUTEX_H
#define _ASM_S390_FUTEX_H

#include <linux/uaccess.h>
#include <linux/futex.h>
#include <asm/mmu_context.h>
#include <asm/errno.h>

#define __futex_atomic_op(insn, ret, oldval, newval, uaddr, oparg)	\
	asm volatile(							\
		"   sacf  256\n"					\
		"0: l     %1,0(%6)\n"					\
		"1:"insn						\
		"2: cs    %1,%2,0(%6)\n"				\
		"3: jl    1b\n"						\
		"   lhi   %0,0\n"					\
		"4: sacf  768\n"					\
		EX_TABLE(0b,4b) EX_TABLE(2b,4b) EX_TABLE(3b,4b)		\
		: "=d" (ret), "=&d" (oldval), "=&d" (newval),		\
		  "=m" (*uaddr)						\
		: "0" (-EFAULT), "d" (oparg), "a" (uaddr),		\
		  "m" (*uaddr) : "cc");

static inline int futex_atomic_op_inuser(int encoded_op, u32 __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, newval, ret;

	load_kernel_asce();
	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	pagefault_disable();
	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("lr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("lr %2,%1\nar %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("lr %2,%1\nor %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("lr %2,%1\nnr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("lr %2,%1\nxr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}
	pagefault_enable();

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

static inline int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
						u32 oldval, u32 newval)
{
	int ret;

	load_kernel_asce();
	asm volatile(
		"   sacf 256\n"
		"0: cs   %1,%4,0(%5)\n"
		"1: la   %0,0\n"
		"2: sacf 768\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+d" (oldval), "=m" (*uaddr)
		: "0" (-EFAULT), "d" (newval), "a" (uaddr), "m" (*uaddr)
		: "cc", "memory");
	*uval = oldval;
	return ret;
}

#endif /* _ASM_S390_FUTEX_H */
