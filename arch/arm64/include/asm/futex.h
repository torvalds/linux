/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_FUTEX_H
#define __ASM_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>

#include <asm/errno.h>
#include <asm/lsui.h>

#define FUTEX_MAX_LOOPS	128 /* What's the largest number you can think of? */

#define LLSC_FUTEX_ATOMIC_OP(op, insn)					\
static __always_inline int						\
__llsc_futex_atomic_##op(int oparg, u32 __user *uaddr, int *oval)	\
{									\
	unsigned int loops = FUTEX_MAX_LOOPS;				\
	int ret, oldval, newval;					\
									\
	uaccess_enable_privileged();					\
	asm volatile("// __llsc_futex_atomic_" #op "\n"			\
"	prfm	pstl1strm, %[uaddr]\n"					\
"1:	ldxr	%w[oldval], %[uaddr]\n"					\
	insn "\n"							\
"2:	stlxr	%w[ret], %w[newval], %[uaddr]\n"			\
"	cbz	%w[ret], 3f\n"						\
"	sub	%w[loops], %w[loops], %w[ret]\n"			\
"	cbnz	%w[loops], 1b\n"					\
"	mov	%w[ret], %w[err]\n"					\
"3:\n"									\
"	dmb	ish\n"							\
	_ASM_EXTABLE_UACCESS_ERR(1b, 3b, %w[ret])			\
	_ASM_EXTABLE_UACCESS_ERR(2b, 3b, %w[ret])			\
	: [ret] "=&r" (ret), [oldval] "=&r" (oldval),			\
	  [uaddr] "+Q" (*uaddr), [newval] "=&r" (newval),		\
	  [loops] "+r" (loops)						\
	: [oparg] "r" (oparg), [err] "Ir" (-EAGAIN)			\
	: "memory");							\
	uaccess_disable_privileged();					\
									\
	if (!ret)							\
		*oval = oldval;						\
									\
	return ret;							\
}

LLSC_FUTEX_ATOMIC_OP(add, "add	%w[newval], %w[oldval], %w[oparg]")
LLSC_FUTEX_ATOMIC_OP(or,  "orr	%w[newval], %w[oldval], %w[oparg]")
LLSC_FUTEX_ATOMIC_OP(and, "and	%w[newval], %w[oldval], %w[oparg]")
LLSC_FUTEX_ATOMIC_OP(eor, "eor	%w[newval], %w[oldval], %w[oparg]")
LLSC_FUTEX_ATOMIC_OP(set, "mov	%w[newval], %w[oparg]")

static __always_inline int
__llsc_futex_cmpxchg(u32 __user *uaddr, u32 oldval, u32 newval, u32 *oval)
{
	int ret = 0;
	unsigned int loops = FUTEX_MAX_LOOPS;
	u32 val, tmp;

	uaccess_enable_privileged();
	asm volatile("//__llsc_futex_cmpxchg\n"
"	prfm	pstl1strm, %[uaddr]\n"
"1:	ldxr	%w[curval], %[uaddr]\n"
"	eor	%w[tmp], %w[curval], %w[oldval]\n"
"	cbnz	%w[tmp], 4f\n"
"2:	stlxr	%w[tmp], %w[newval], %[uaddr]\n"
"	cbz	%w[tmp], 3f\n"
"	sub	%w[loops], %w[loops], %w[tmp]\n"
"	cbnz	%w[loops], 1b\n"
"	mov	%w[ret], %w[err]\n"
"3:\n"
"	dmb	ish\n"
"4:\n"
	_ASM_EXTABLE_UACCESS_ERR(1b, 4b, %w[ret])
	_ASM_EXTABLE_UACCESS_ERR(2b, 4b, %w[ret])
	: [ret] "+r" (ret), [curval] "=&r" (val),
	  [uaddr] "+Q" (*uaddr), [tmp] "=&r" (tmp),
	  [loops] "+r" (loops)
	: [oldval] "r" (oldval), [newval] "r" (newval),
	  [err] "Ir" (-EAGAIN)
	: "memory");
	uaccess_disable_privileged();

	if (!ret)
		*oval = val;

	return ret;
}

#ifdef CONFIG_ARM64_LSUI

/*
 * Wrap LSUI instructions with uaccess_ttbr0_enable()/disable(), as
 * PAN toggling is not required.
 */

#define LSUI_FUTEX_ATOMIC_OP(op, asm_op)				\
static __always_inline int						\
__lsui_futex_atomic_##op(int oparg, u32 __user *uaddr, int *oval)	\
{									\
	int ret = 0;							\
	int oldval;							\
									\
	uaccess_ttbr0_enable();						\
									\
	asm volatile("// __lsui_futex_atomic_" #op "\n"			\
	__LSUI_PREAMBLE							\
"1:	" #asm_op "al	%w[oparg], %w[oldval], %[uaddr]\n"		\
"2:\n"									\
	_ASM_EXTABLE_UACCESS_ERR(1b, 2b, %w[ret])			\
	: [ret] "+r" (ret), [uaddr] "+Q" (*uaddr),			\
	  [oldval] "=r" (oldval)					\
	: [oparg] "r" (oparg)						\
	: "memory");							\
									\
	uaccess_ttbr0_disable();					\
									\
	if (!ret)							\
		*oval = oldval;						\
	return ret;							\
}

LSUI_FUTEX_ATOMIC_OP(add, ldtadd)
LSUI_FUTEX_ATOMIC_OP(or, ldtset)
LSUI_FUTEX_ATOMIC_OP(andnot, ldtclr)
LSUI_FUTEX_ATOMIC_OP(set, swpt)

static __always_inline int
__lsui_cmpxchg64(u64 __user *uaddr, u64 *oldval, u64 newval)
{
	int ret = 0;

	uaccess_ttbr0_enable();

	asm volatile("// __lsui_cmpxchg64\n"
	__LSUI_PREAMBLE
"1:	casalt	%[oldval], %[newval], %[uaddr]\n"
"2:\n"
	_ASM_EXTABLE_UACCESS_ERR(1b, 2b, %w[ret])
	: [ret] "+r" (ret), [uaddr] "+Q" (*uaddr),
	  [oldval] "+r" (*oldval)
	: [newval] "r" (newval)
	: "memory");

	uaccess_ttbr0_disable();

	return ret;
}

static __always_inline int
__lsui_cmpxchg32(u32 __user *uaddr, u32 oldval, u32 newval, u32 *oval)
{
	u64 __user *uaddr64;
	bool futex_pos, other_pos;
	u32 other, orig_other;
	union {
		u32 futex[2];
		u64 raw;
	} oval64, orig64, nval64;

	uaddr64 = (u64 __user *)PTR_ALIGN_DOWN(uaddr, sizeof(u64));
	futex_pos = !IS_ALIGNED((unsigned long)uaddr, sizeof(u64));
	other_pos = !futex_pos;

	oval64.futex[futex_pos] = oldval;
	if (get_user(oval64.futex[other_pos], (u32 __user *)uaddr64 + other_pos))
		return -EFAULT;

	orig64.raw = oval64.raw;

	nval64.futex[futex_pos] = newval;
	nval64.futex[other_pos] = oval64.futex[other_pos];

	if (__lsui_cmpxchg64(uaddr64, &oval64.raw, nval64.raw))
		return -EFAULT;

	oldval = oval64.futex[futex_pos];
	other = oval64.futex[other_pos];
	orig_other = orig64.futex[other_pos];

	if (other != orig_other)
		return -EAGAIN;

	*oval = oldval;

	return 0;
}

static __always_inline int
__lsui_futex_atomic_and(int oparg, u32 __user *uaddr, int *oval)
{
	/*
	 * Undo the bitwise negation applied to the oparg passed from
	 * arch_futex_atomic_op_inuser() with FUTEX_OP_ANDN.
	 */
	return __lsui_futex_atomic_andnot(~oparg, uaddr, oval);
}

static __always_inline int
__lsui_futex_atomic_eor(int oparg, u32 __user *uaddr, int *oval)
{
	u32 oldval, newval, val;
	int ret, i;

	if (get_user(oldval, uaddr))
		return -EFAULT;

	/*
	 * there are no ldteor/stteor instructions...
	 */
	for (i = 0; i < FUTEX_MAX_LOOPS; i++) {
		newval = oldval ^ oparg;

		ret = __lsui_cmpxchg32(uaddr, oldval, newval, &val);
		switch (ret) {
		case -EFAULT:
			return ret;
		case -EAGAIN:
			continue;
		}

		if (val == oldval) {
			*oval = val;
			return 0;
		}

		oldval = val;
	}

	return -EAGAIN;
}

static __always_inline int
__lsui_futex_cmpxchg(u32 __user *uaddr, u32 oldval, u32 newval, u32 *oval)
{
	/*
	 * Callers of futex_atomic_cmpxchg_inatomic() already retry on
	 * -EAGAIN, no need for another loop of max retries.
	 */
	return __lsui_cmpxchg32(uaddr, oldval, newval, oval);
}
#endif	/* CONFIG_ARM64_LSUI */


#define FUTEX_ATOMIC_OP(op)						\
static __always_inline int						\
__futex_atomic_##op(int oparg, u32 __user *uaddr, int *oval)		\
{									\
	return __lsui_llsc_body(futex_atomic_##op, oparg, uaddr, oval);	\
}

FUTEX_ATOMIC_OP(add)
FUTEX_ATOMIC_OP(or)
FUTEX_ATOMIC_OP(and)
FUTEX_ATOMIC_OP(eor)
FUTEX_ATOMIC_OP(set)

static __always_inline int
__futex_cmpxchg(u32 __user *uaddr, u32 oldval, u32 newval, u32 *oval)
{
	return __lsui_llsc_body(futex_cmpxchg, uaddr, oldval, newval, oval);
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *_uaddr)
{
	int ret;
	u32 __user *uaddr;

	if (!access_ok(_uaddr, sizeof(u32)))
		return -EFAULT;

	uaddr = __uaccess_mask_ptr(_uaddr);

	switch (op) {
	case FUTEX_OP_SET:
		ret = __futex_atomic_set(oparg, uaddr, oval);
		break;
	case FUTEX_OP_ADD:
		ret = __futex_atomic_add(oparg, uaddr, oval);
		break;
	case FUTEX_OP_OR:
		ret = __futex_atomic_or(oparg, uaddr, oval);
		break;
	case FUTEX_OP_ANDN:
		ret = __futex_atomic_and(~oparg, uaddr, oval);
		break;
	case FUTEX_OP_XOR:
		ret = __futex_atomic_eor(oparg, uaddr, oval);
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *_uaddr,
			      u32 oldval, u32 newval)
{
	u32 __user *uaddr;

	if (!access_ok(_uaddr, sizeof(u32)))
		return -EFAULT;

	uaddr = __uaccess_mask_ptr(_uaddr);

	return __futex_cmpxchg(uaddr, oldval, newval, uval);
}

#endif /* __ASM_FUTEX_H */
