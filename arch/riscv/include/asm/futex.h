/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2006  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (c) 2018  Jim Wilson (jimw@sifive.com)
 */

#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#ifndef CONFIG_RISCV_ISA_A
/*
 * Use the generic interrupt disabling versions if the A extension
 * is not supported.
 */
#ifdef CONFIG_SMP
#error "Can't support generic futex calls without A extension on SMP"
#endif
#include <asm-generic/futex.h>

#else /* CONFIG_RISCV_ISA_A */

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <asm/asm.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)	\
{								\
	uintptr_t tmp;						\
	__enable_user_access();					\
	__asm__ __volatile__ (					\
	"1:	" insn "				\n"	\
	"2:						\n"	\
	"	.section .fixup,\"ax\"			\n"	\
	"	.balign 4				\n"	\
	"3:	li %[r],%[e]				\n"	\
	"	jump 2b,%[t]				\n"	\
	"	.previous				\n"	\
	"	.section __ex_table,\"a\"		\n"	\
	"	.balign " RISCV_SZPTR "			\n"	\
	"	" RISCV_PTR " 1b, 3b			\n"	\
	"	.previous				\n"	\
	: [r] "+r" (ret), [ov] "=&r" (oldval),			\
	  [u] "+m" (*uaddr), [t] "=&r" (tmp)			\
	: [op] "Jr" (oparg), [e] "i" (-EFAULT)			\
	: "memory");						\
	__disable_user_access();				\
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret = 0;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("amoswap.w.aqrl %[ov],%z[op],%[u]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("amoadd.w.aqrl %[ov],%z[op],%[u]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("amoor.w.aqrl %[ov],%z[op],%[u]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("amoand.w.aqrl %[ov],%z[op],%[u]",
				  ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("amoxor.w.aqrl %[ov],%z[op],%[u]",
				  ret, oldval, uaddr, oparg);
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
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val;
	uintptr_t tmp;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	__enable_user_access();
	__asm__ __volatile__ (
	"1:	lr.w.aqrl %[v],%[u]			\n"
	"	bne %[v],%z[ov],3f			\n"
	"2:	sc.w.aqrl %[t],%z[nv],%[u]		\n"
	"	bnez %[t],1b				\n"
	"3:						\n"
	"	.section .fixup,\"ax\"			\n"
	"	.balign 4				\n"
	"4:	li %[r],%[e]				\n"
	"	jump 3b,%[t]				\n"
	"	.previous				\n"
	"	.section __ex_table,\"a\"		\n"
	"	.balign " RISCV_SZPTR "			\n"
	"	" RISCV_PTR " 1b, 4b			\n"
	"	" RISCV_PTR " 2b, 4b			\n"
	"	.previous				\n"
	: [r] "+r" (ret), [v] "=&r" (val), [u] "+m" (*uaddr), [t] "=&r" (tmp)
	: [ov] "Jr" (oldval), [nv] "Jr" (newval), [e] "i" (-EFAULT)
	: "memory");
	__disable_user_access();

	*uval = val;
	return ret;
}

#endif /* CONFIG_RISCV_ISA_A */
#endif /* _ASM_FUTEX_H */
