/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_FUTEX_H
#define _ASM_S390_FUTEX_H

#include <linux/instrumented.h>
#include <linux/uaccess.h>
#include <linux/futex.h>
#include <asm/asm-extable.h>
#include <asm/mmu_context.h>
#include <asm/errno.h>

#define FUTEX_OP_FUNC(name, insn)						\
static uaccess_kmsan_or_inline int						\
__futex_atomic_##name(int oparg, int *old, u32 __user *uaddr)			\
{										\
	int rc, new;								\
										\
	instrument_copy_from_user_before(old, uaddr, sizeof(*old));		\
	asm_inline volatile(							\
		"	sacf	256\n"						\
		"0:	l	%[old],%[uaddr]\n"				\
		"1:"insn							\
		"2:	cs	%[old],%[new],%[uaddr]\n"			\
		"3:	jl	1b\n"						\
		"	lhi	%[rc],0\n"					\
		"4:	sacf	768\n"						\
		EX_TABLE_UA_FAULT(0b, 4b, %[rc])				\
		EX_TABLE_UA_FAULT(1b, 4b, %[rc])				\
		EX_TABLE_UA_FAULT(2b, 4b, %[rc])				\
		EX_TABLE_UA_FAULT(3b, 4b, %[rc])				\
		: [rc] "=d" (rc), [old] "=&d" (*old),				\
		  [new] "=&d" (new), [uaddr] "+Q" (*uaddr)			\
		: [oparg] "d" (oparg)						\
		: "cc");							\
	if (!rc)								\
		instrument_copy_from_user_after(old, uaddr, sizeof(*old), 0);	\
	return rc;								\
}

FUTEX_OP_FUNC(set, "lr %[new],%[oparg]\n")
FUTEX_OP_FUNC(add, "lr %[new],%[old]\n ar %[new],%[oparg]\n")
FUTEX_OP_FUNC(or,  "lr %[new],%[old]\n or %[new],%[oparg]\n")
FUTEX_OP_FUNC(and, "lr %[new],%[old]\n nr %[new],%[oparg]\n")
FUTEX_OP_FUNC(xor, "lr %[new],%[old]\n xr %[new],%[oparg]\n")

static inline
int arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int old, rc;

	switch (op) {
	case FUTEX_OP_SET:
		rc = __futex_atomic_set(oparg, &old, uaddr);
		break;
	case FUTEX_OP_ADD:
		rc = __futex_atomic_add(oparg, &old, uaddr);
		break;
	case FUTEX_OP_OR:
		rc = __futex_atomic_or(oparg, &old, uaddr);
		break;
	case FUTEX_OP_ANDN:
		rc = __futex_atomic_and(~oparg, &old, uaddr);
		break;
	case FUTEX_OP_XOR:
		rc = __futex_atomic_xor(oparg, &old, uaddr);
		break;
	default:
		rc = -ENOSYS;
	}
	if (!rc)
		*oval = old;
	return rc;
}

static uaccess_kmsan_or_inline
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval)
{
	int rc;

	instrument_copy_from_user_before(uval, uaddr, sizeof(*uval));
	asm_inline volatile(
		"	sacf	256\n"
		"0:	cs	%[old],%[new],%[uaddr]\n"
		"1:	lhi	%[rc],0\n"
		"2:	sacf	768\n"
		EX_TABLE_UA_FAULT(0b, 2b, %[rc])
		EX_TABLE_UA_FAULT(1b, 2b, %[rc])
		: [rc] "=d" (rc), [old] "+d" (oldval), [uaddr] "+Q" (*uaddr)
		: [new] "d" (newval)
		: "cc", "memory");
	*uval = oldval;
	instrument_copy_from_user_after(uval, uaddr, sizeof(*uval), 0);
	return rc;
}

#endif /* _ASM_S390_FUTEX_H */
